// HTTP for the mining tap (mining/mining.h) through NSURLSession.
#include "platform/apple/apple_http.h"

#import <Foundation/Foundation.h>
#include <TargetConditionals.h>

#include <algorithm>

#include "platform/apple/apple_platform.h"

namespace {

NSURLSession *ore_http_session() {
	static NSURLSession *session;
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		// Nothing is kept between requests: no cookies, no cache, no credentials.
		NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
		config.HTTPCookieStorage = nil;
		config.URLCache = nil;
		config.HTTPShouldSetCookies = NO;
		config.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
		config.timeoutIntervalForResource = 60;
		session = [NSURLSession sessionWithConfiguration:config];
	});
	return session;
}

}  // namespace

// One request and what became of it. An activity that runs out of time
// cancels it from another thread, hence the locks.
@interface OreHttpCall : NSObject
- (instancetype)initWithRequest:(NSURLRequest *)request body:(NSData *)body;
// True for the first caller only.
- (BOOL)claim;
// Sends the request and waits for the answer.
- (void)run;
- (void)cancel;
// The HTTP status, or 0 when no answer came.
- (int)status;
- (NSData *)answer;
- (NSString *)failure;
@end

@implementation OreHttpCall {
	NSURLRequest *_request;
	NSData *_body;
	NSURLSessionTask *_task;
	BOOL _claimed;
	BOOL _cancelled;
	int _status;
	NSData *_answer;
	NSString *_failure;
}

- (instancetype)initWithRequest:(NSURLRequest *)request body:(NSData *)body {
	self = [super init];
	if (self) {
		_request = request;
		_body = body;
	}
	return self;
}

- (BOOL)claim {
	@synchronized(self) {
		if (_claimed) return NO;
		_claimed = YES;
		return YES;
	}
}

- (void)run {
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	void (^finished)(NSData *, NSURLResponse *, NSError *) = ^(NSData *data, NSURLResponse *response, NSError *error) {
		@synchronized(self) {
			if (!error && [response isKindOfClass:[NSHTTPURLResponse class]]) {
				self->_status = (int)((NSHTTPURLResponse *)response).statusCode;
				self->_answer = data;
			} else {
				self->_status = 0;
				self->_failure = error ? error.localizedDescription : @"The answer is not one of a web server.";
			}
		}
		dispatch_semaphore_signal(done);
	};
	@synchronized(self) {
		if (_cancelled) {
			_failure = @"The app is being put to sleep.";
			return;
		}
		NSURLSession *session = ore_http_session();
		_task = _body ? [session uploadTaskWithRequest:_request fromData:_body completionHandler:finished]
		              : [session dataTaskWithRequest:_request completionHandler:finished];
		[_task resume];
	}
	dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
}

- (void)cancel {
	@synchronized(self) {
		_cancelled = YES;
		[_task cancel];
	}
}

- (int)status {
	@synchronized(self) {
		return _status;
	}
}

- (NSData *)answer {
	@synchronized(self) {
		return _answer;
	}
}

- (NSString *)failure {
	@synchronized(self) {
		return _failure;
	}
}

@end

namespace ore {
namespace {

int apple_http_send(const HttpRequest &request, std::string &answer) {
	@autoreleasepool {
		answer.clear();
		NSString *address = [NSString stringWithUTF8String:request.url.c_str()];
		NSURL *url = address ? [NSURL URLWithString:address] : nil;
		if (!url) {
			answer = "The address is not one.";
			return 0;
		}
		NSMutableURLRequest *message = [NSMutableURLRequest requestWithURL:url];
		NSString *method = [NSString stringWithUTF8String:request.method.c_str()];
		message.HTTPMethod = method ? method : @"POST";
		message.timeoutInterval = std::max(1, request.timeout_ms) / 1000.0;
		for (const auto &header : request.headers) {
			NSString *name = [NSString stringWithUTF8String:header.first.c_str()];
			NSString *value = [NSString stringWithUTF8String:header.second.c_str()];
			if (name && value) [message setValue:value forHTTPHeaderField:name];
		}
		NSData *body = request.body.empty() ? nil : [NSData dataWithBytes:request.body.data() length:request.body.size()];
		OreHttpCall *call = [[OreHttpCall alloc] initWithRequest:message body:body];
#if TARGET_OS_IPHONE
		// The player may switch to the mining page the moment a line stands on the screen:
		// ask the system for the time to finish sending it before the app is put to sleep.
		// The block comes once with expired = NO to do the work, and again with YES when
		// the time runs out; with YES alone when no time is to be had.
		dispatch_semaphore_t over = dispatch_semaphore_create(0);
		[[NSProcessInfo processInfo] performExpiringActivityWithReason:@"Sending a line to the mining server"
		                                                    usingBlock:^(BOOL expired) {
			                                                    if (expired) [call cancel];
			                                                    if (![call claim]) return;
			                                                    if (!expired) [call run];
			                                                    dispatch_semaphore_signal(over);
		                                                    }];
		dispatch_semaphore_wait(over, DISPATCH_TIME_FOREVER);
#else
		[call run];
#endif
		int status = [call status];
		if (status > 0) {
			NSData *data = [call answer];
			if (data.length) answer.assign(static_cast<const char *>(data.bytes), std::min<size_t>(data.length, 4096));
		} else {
			NSString *why = [call failure];
			const char *text = why ? why.UTF8String : nullptr;
			answer = text ? text : "";
		}
		return status;
	}
}

}  // namespace

HttpSend apple_http_sender() {
	return apple_http_send;
}

HttpSend ApplePlatform::http_sender() {
	return apple_http_sender();
}

}  // namespace ore
