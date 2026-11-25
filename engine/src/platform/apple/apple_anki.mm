// The Anki app beside this one on an iPhone or iPad: AnkiMobile.
#include "platform/apple/apple_anki.h"

#include <TargetConditionals.h>

#import <Foundation/Foundation.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#endif

#include "platform/apple/apple_platform.h"

namespace ore {

#if TARGET_OS_IPHONE
namespace {

// Both belong to the main thread.
UIBackgroundTaskIdentifier g_task = UIBackgroundTaskInvalid;
bool g_away = false;

void end_task() {
	if (g_task == UIBackgroundTaskInvalid) return;
	UIBackgroundTaskIdentifier task = g_task;
	g_task = UIBackgroundTaskInvalid;
	[[UIApplication sharedApplication] endBackgroundTask:task];
}

// Once: hears when the app goes to the back and comes to the front again.
void watch_app() {
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
		[center addObserverForName:UIApplicationDidEnterBackgroundNotification
		                    object:nil
		                     queue:[NSOperationQueue mainQueue]
		                usingBlock:^(NSNotification *note) {
			                g_away = true;
		                }];
		[center addObserverForName:UIApplicationDidBecomeActiveNotification
		                    object:nil
		                     queue:[NSOperationQueue mainQueue]
		                usingBlock:^(NSNotification *note) {
			                if (!g_away) return;
			                g_away = false;
			                end_task();
			                AppReturns::shared().in_front();
		                }];
	});
}

// AnkiMobile fetches a card's files from this app while this app is in the
// back, so the system is asked for the time to stay awake until it returns.
void open_link(const std::string &link, std::function<void(bool opened)> done) {
	NSString *text = [[NSString alloc] initWithBytes:link.data() length:link.size() encoding:NSUTF8StringEncoding];
	NSURL *url = text ? [NSURL URLWithString:text] : nil;
	if (!url) {
		if (done) done(false);
		return;
	}
	dispatch_async(dispatch_get_main_queue(), ^{
		watch_app();
		UIApplication *app = [UIApplication sharedApplication];
		end_task();
		g_task = [app beginBackgroundTaskWithName:@"A card for AnkiMobile"
		                        expirationHandler:^{
			                        end_task();
		                        }];
		[app openURL:url
		              options:@{}
		    completionHandler:^(BOOL opened) {
			    if (!opened) end_task();
			    if (done) done(opened);
		    }];
	});
}

// AnkiMobile's manual: the answer lies on the clipboard under this type, and
// whoever reads it empties it.
std::string take_answer() {
	__block std::string out;
	void (^work)(void) = ^{
		UIPasteboard *board = [UIPasteboard generalPasteboard];
		NSString *type = [NSString stringWithUTF8String:kAnkiMobileClipboardType];
		NSData *data = [board dataForPasteboardType:type];
		if (data.length == 0) return;
		out.assign(static_cast<const char *>(data.bytes), data.length);
		[board setData:[NSData data] forPasteboardType:type];
	};
	if ([NSThread isMainThread]) {
		work();
	} else {
		dispatch_sync(dispatch_get_main_queue(), work);
	}
	return out;
}

}  // namespace
#endif

AnkiAppHost apple_anki_host() {
	AnkiAppHost host;
#if TARGET_OS_IPHONE
	host.open_link = open_link;
	host.take_answer = take_answer;
	dispatch_async(dispatch_get_main_queue(), ^{
		watch_app();
	});
#endif
	return host;
}

AnkiAppHost ApplePlatform::anki_app() { return apple_anki_host(); }

}  // namespace ore
