// What the mining kit needs of the system (mining/kit_host.h) on Apple
// devices: HTTP through NSURLSession, and a line's voice from Ogg to AAC,
// which every Anki plays (AnkiMobile does not play Ogg).
#include "mining/kit_host.h"
#if ORE_MINING_KIT

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "audio/mixer.h"

// One request under way. The session's delegate fills it in on the session's
// queue; the thread that asked waits for `done` and reads it after.
@interface OreFetchCall : NSObject {
@public
	const ore::FetchRequest *request;
	int status;
	NSString *type;
	NSString *from;
	NSMutableData *body;
	FILE *file;
	uint64_t taken, expected;
	BOOL tooLarge, writeFailed;
	NSError *error;
	dispatch_semaphore_t done;
}
@end

@implementation OreFetchCall
@end

@interface OreFetchHub : NSObject <NSURLSessionDataDelegate>
+ (OreFetchHub *)shared;
- (void)run:(const ore::FetchRequest &)request answer:(ore::FetchAnswer &)answer;
@end

@implementation OreFetchHub {
	NSURLSession *_session;
	NSMutableDictionary<NSNumber *, OreFetchCall *> *_calls;
}

+ (OreFetchHub *)shared {
	static OreFetchHub *hub;
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		hub = [[OreFetchHub alloc] init];
	});
	return hub;
}

- (instancetype)init {
	self = [super init];
	if (self) {
		// Nothing is kept between requests: no cookies, no cache, no credentials.
		NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
		config.HTTPCookieStorage = nil;
		config.URLCache = nil;
		config.HTTPShouldSetCookies = NO;
		config.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
		// A large dictionary over a slow line takes its time.
		config.timeoutIntervalForResource = 3600;
		_calls = [NSMutableDictionary dictionary];
		_session = [NSURLSession sessionWithConfiguration:config delegate:self delegateQueue:nil];
	}
	return self;
}

- (OreFetchCall *)callOf:(NSURLSessionTask *)task {
	@synchronized(self) {
		return _calls[@(task.taskIdentifier)];
	}
}

- (void)run:(const ore::FetchRequest &)request answer:(ore::FetchAnswer &)answer {
	answer = ore::FetchAnswer();
	NSString *address = [NSString stringWithUTF8String:request.url.c_str()];
	NSURL *url = address ? [NSURL URLWithString:address] : nil;
	if (!url) {
		answer.error = "The address is not one.";
		return;
	}
	NSMutableURLRequest *message = [NSMutableURLRequest requestWithURL:url];
	NSString *method = [NSString stringWithUTF8String:request.method.c_str()];
	message.HTTPMethod = method.length ? method : @"GET";
	message.timeoutInterval = std::max(1, request.timeout_ms) / 1000.0;
	for (const auto &header : request.headers) {
		NSString *name = [NSString stringWithUTF8String:header.first.c_str()];
		NSString *value = [NSString stringWithUTF8String:header.second.c_str()];
		if (name && value) [message setValue:value forHTTPHeaderField:name];
	}
	if (!request.body.empty()) message.HTTPBody = [NSData dataWithBytes:request.body.data() length:request.body.size()];

	OreFetchCall *call = [[OreFetchCall alloc] init];
	call->request = &request;
	call->body = [NSMutableData data];
	call->done = dispatch_semaphore_create(0);
	NSURLSessionDataTask *task = [_session dataTaskWithRequest:message];
	@synchronized(self) {
		_calls[@(task.taskIdentifier)] = call;
	}
	[task resume];
	// The request is given up from here; its end still comes through the delegate.
	while (dispatch_semaphore_wait(call->done, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC)) != 0) {
		if (request.stop && request.stop->load()) [task cancel];
	}

	if (call->file) fclose(call->file);
	bool whole = !call->error && !call->writeFailed && !call->tooLarge;
	if (!whole && !request.to_file.empty()) remove(request.to_file.c_str());
	if (call->tooLarge) {
		answer.status = call->status;
		answer.too_large = true;
	} else if (call->writeFailed) {
		answer.error = "The file cannot be written.";
	} else if (call->error || call->status == 0) {
		const char *why = call->error ? call->error.localizedDescription.UTF8String : nullptr;
		answer.error = why ? why : "The answer is not one of a web server.";
	} else {
		answer.status = call->status;
		answer.body.assign(static_cast<const char *>(call->body.bytes), call->body.length);
	}
	const char *type = call->type.UTF8String, *from = call->from.UTF8String;
	answer.content_type = type ? type : "";
	answer.final_url = from ? from : request.url;
}

- (void)URLSession:(NSURLSession *)session
              dataTask:(NSURLSessionDataTask *)task
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))handler {
	OreFetchCall *call = [self callOf:task];
	if (call && [response isKindOfClass:[NSHTTPURLResponse class]]) {
		NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
		call->status = (int)http.statusCode;
		call->type = http.MIMEType.lowercaseString;
		call->from = http.URL.absoluteString;
		call->expected = http.expectedContentLength > 0 ? uint64_t(http.expectedContentLength) : 0;
		const std::string &to_file = call->request->to_file;
		if (!to_file.empty() && call->status >= 200 && call->status < 300) {
			call->file = fopen(to_file.c_str(), "wb");
			if (!call->file) {
				call->writeFailed = YES;
				handler(NSURLSessionResponseCancel);
				return;
			}
		}
	}
	handler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data {
	OreFetchCall *call = [self callOf:task];
	if (!call || call->tooLarge || call->writeFailed) return;
	if (call->taken + data.length > call->request->max_bytes) {
		call->tooLarge = YES;
		[task cancel];
		return;
	}
	if (call->file) {
		__block BOOL written = YES;
		[data enumerateByteRangesUsingBlock:^(const void *bytes, NSRange range, BOOL *stop) {
			if (fwrite(bytes, 1, range.length, call->file) != range.length) {
				written = NO;
				*stop = YES;
			}
		}];
		if (!written) {
			call->writeFailed = YES;
			[task cancel];
			return;
		}
	} else {
		[call->body appendData:data];
	}
	call->taken += data.length;
	if (call->request->progress && call->expected > 0) call->request->progress(std::min(1.0, double(call->taken) / double(call->expected)));
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
	OreFetchCall *call = [self callOf:task];
	if (!call) return;
	@synchronized(self) {
		[_calls removeObjectForKey:@(task.taskIdentifier)];
	}
	call->error = error;
	dispatch_semaphore_signal(call->done);
}

@end

namespace ore {
namespace {

void apple_fetch(const FetchRequest &request, FetchAnswer &answer) {
	@autoreleasepool {
		[[OreFetchHub shared] run:request answer:answer];
	}
}

// The game's voices are Ogg Vorbis: decoded with the engine's own decoder
// and written as AAC in an MPEG-4 file by the system.
bool apple_encode_voice(const std::string &file, const std::string &ending, std::string &out, std::string &type, std::string &out_ending) {
	if (ending != "ogg") return false;
	std::shared_ptr<PcmBuffer> pcm = decode_ogg(std::vector<uint8_t>(file.begin(), file.end()));
	if (!pcm || pcm->frames() == 0) return false;
	@autoreleasepool {
		NSString *name = [NSString stringWithFormat:@"ore-voice-%@.m4a", [NSUUID UUID].UUIDString];
		NSURL *url = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:name]];
		AudioStreamBasicDescription aac = {};
		aac.mFormatID = kAudioFormatMPEG4AAC;
		aac.mSampleRate = pcm->rate;
		aac.mChannelsPerFrame = 2;
		ExtAudioFileRef writer = nullptr;
		if (ExtAudioFileCreateWithURL((__bridge CFURLRef)url, kAudioFileM4AType, &aac, nullptr, kAudioFileFlags_EraseFile, &writer) != noErr || !writer)
			return false;
		AudioStreamBasicDescription plain = {};
		plain.mFormatID = kAudioFormatLinearPCM;
		plain.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
		plain.mSampleRate = pcm->rate;
		plain.mChannelsPerFrame = 2;
		plain.mBitsPerChannel = 16;
		plain.mBytesPerFrame = 4;
		plain.mFramesPerPacket = 1;
		plain.mBytesPerPacket = 4;
		OSStatus status = ExtAudioFileSetProperty(writer, kExtAudioFileProperty_ClientDataFormat, sizeof plain, &plain);
		size_t frames = pcm->frames(), at = 0;
		while (status == noErr && at < frames) {
			UInt32 count = UInt32(std::min<size_t>(frames - at, 8192));
			AudioBufferList list;
			list.mNumberBuffers = 1;
			list.mBuffers[0].mNumberChannels = 2;
			list.mBuffers[0].mDataByteSize = count * 4;
			list.mBuffers[0].mData = pcm->samples.data() + at * 2;
			status = ExtAudioFileWrite(writer, count, &list);
			at += count;
		}
		ExtAudioFileDispose(writer);
		NSData *bytes = status == noErr ? [NSData dataWithContentsOfURL:url] : nil;
		[[NSFileManager defaultManager] removeItemAtURL:url error:nil];
		if (bytes.length == 0) return false;
		out.assign(static_cast<const char *>(bytes.bytes), bytes.length);
		type = "audio/mp4";
		out_ending = "m4a";
		return true;
	}
}

}  // namespace

HttpFetch host_http_fetcher() {
	return apple_fetch;
}

VoiceEncoder host_voice_encoder() {
	return apple_encode_voice;
}

}  // namespace ore

#endif  // ORE_MINING_KIT
