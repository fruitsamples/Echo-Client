/*
	Copyright: 	© Copyright 2002 Apple Computer, Inc. All rights reserved.

	Disclaimer:	IMPORTANT:  This Apple software is supplied to you by Apple Computer, Inc.
			("Apple") in consideration of your agreement to the following terms, and your
			use, installation, modification or redistribution of this Apple software
			constitutes acceptance of these terms.  If you do not agree with these terms,
			please do not use, install, modify or redistribute this Apple software.

			In consideration of your agreement to abide by the following terms, and subject
			to these terms, Apple grants you a personal, non-exclusive license, under Apple’s
			copyrights in this original Apple software (the "Apple Software"), to use,
			reproduce, modify and redistribute the Apple Software, with or without
			modifications, in source and/or binary forms; provided that if you redistribute
			the Apple Software in its entirety and without modifications, you must retain
			this notice and the following text and disclaimers in all such redistributions of
			the Apple Software.  Neither the name, trademarks, service marks or logos of
			Apple Computer, Inc. may be used to endorse or promote products derived from the
			Apple Software without specific prior written permission from Apple.  Except as
			expressly stated in this notice, no other rights or licenses, express or implied,
			are granted by Apple herein, including but not limited to any patent rights that
			may be infringed by your derivative works or by other works in which the Apple
			Software may be incorporated.

			The Apple Software is provided by Apple on an "AS IS" basis.  APPLE MAKES NO
			WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED
			WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
			PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND OPERATION ALONE OR IN
			COMBINATION WITH YOUR PRODUCTS.

			IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR
			CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
			GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
			ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION
			OF THE APPLE SOFTWARE, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT
			(INCLUDING NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN
			ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma mark Includes
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

#include <sys/socket.h>
#include <netinet/in.h>


#pragma mark -
#pragma mark Constant Definitions

#define kServiceType	CFSTR("_echo._tcp.")


#pragma mark -
#pragma mark Global Definitions


CFNetServiceBrowserRef gDomainBrowser = NULL;
CFMutableDictionaryRef gDiscoveredDomains = NULL;


#pragma mark -
#pragma mark Static Function Declarations

static void ServiceBrowserCallBack(CFNetServiceBrowserRef browser, CFOptionFlags flags, CFNetServiceRef service, CFStreamError* error, void* info);
static void DomainBrowserCallBack(CFNetServiceBrowserRef browser, CFOptionFlags flags, CFStringRef domain, CFStreamError* error, void* info);
static void BrowseForServers(void);

static void ServiceCallBack(CFNetServiceRef theService, CFStreamError* error, void* info);
static void RunEchoOnStreams(CFReadStreamRef rStream, CFWriteStreamRef wStream);
static void ConnectAndUseService(const char* nameAndDomain);


#pragma mark -
#pragma mark Static Function Definitions

/* static */ void
ServiceBrowserCallBack(CFNetServiceBrowserRef browser, CFOptionFlags flags, CFNetServiceRef service, CFStreamError* error, void* info) {

	assert(!(flags & kCFNetServiceFlagIsDomain));
	
    // Only prints out add's
	if ((flags & kCFNetServiceFlagRemove) == 0) {
        
        char c[4096];		// Big enough buffer to hold the name and domain
        
        // Concatenate the name and domain together.
        CFStringRef output = CFStringCreateWithFormat(kCFAllocatorDefault,
                                                      NULL,
                                                      CFSTR("%@.%@\n"),
                                                      CFNetServiceGetName(service),
                                                      CFNetServiceGetDomain(service));
        
        // Try to pull the string out for display to stdout
        if (CFStringGetCString(output, c, sizeof(c), kCFStringEncodingUTF8))
            fprintf(stdout, c);
            
        // Release the string.
        CFRelease(output);
    }
}


/* static */ void
DomainBrowserCallBack(CFNetServiceBrowserRef browser, CFOptionFlags flags, CFStringRef domain, CFStreamError* error, void* info) {

	assert(flags & kCFNetServiceFlagIsDomain);
    
    CFNetServiceBrowserRef serviceBrowser;

    // Check to see if removing the domain from the list.
	if (flags & kCFNetServiceFlagRemove) {
        
        // Get the service browser that exists for this domain.
        if (CFDictionaryGetValueIfPresent(gDiscoveredDomains, domain, (const void **)&serviceBrowser)) {
        
            // Found the browser so unschedule and invalidate it so no callbacks occur.
            CFNetServiceBrowserUnscheduleFromRunLoop(serviceBrowser, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
            CFNetServiceBrowserInvalidate(serviceBrowser);
        }
        
        // Remove the domain from the list.
        CFDictionaryRemoveValue(gDiscoveredDomains, domain);
    }
    
	else {

		CFNetServiceClientContext serviceBrowserCtxt = {0, NULL, NULL, NULL, NULL};
        
        // Create a service browser in order to find echo services on this new domain.
        serviceBrowser = CFNetServiceBrowserCreate(kCFAllocatorDefault,
                                                   (CFNetServiceBrowserClientCallBack)&ServiceBrowserCallBack,
                                                   &serviceBrowserCtxt);
		
        // Only start the search if the creation succeeded.
		if (serviceBrowser != NULL) {
			
            // Schedule it on the run loop
			CFNetServiceBrowserScheduleWithRunLoop(serviceBrowser, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
			
            // Start the search and add this browser to the list of searches
			if (CFNetServiceBrowserSearchForServices(serviceBrowser, domain, kServiceType, NULL))
				CFDictionarySetValue(gDiscoveredDomains, domain, serviceBrowser);

            // Release the browser.  CFDictionary will have retained it if it was added.
			CFRelease(serviceBrowser);
		}
	}
}


/* static */ void
BrowseForServers(void) {

	CFNetServiceClientContext domainBrowserCtxt = {0, NULL, NULL, NULL, NULL};
	
    // Global set of found domain names as keys and the search for
    // services in that domain as the value.
	gDiscoveredDomains = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                   0,
                                                   &kCFCopyStringDictionaryKeyCallBacks,
                                                   &kCFTypeDictionaryValueCallBacks);
    
    // Create the global browser which is searching for domains
	gDomainBrowser = CFNetServiceBrowserCreate(kCFAllocatorDefault,
                                               (CFNetServiceBrowserClientCallBack)&DomainBrowserCallBack,
                                               &domainBrowserCtxt);

    // Start everything and succeed only if both were created.
	if ((gDomainBrowser != NULL) && (gDiscoveredDomains != NULL)) {
		
        // Schedule
		CFNetServiceBrowserScheduleWithRunLoop(gDomainBrowser, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);

        // Search for all domains
		if (CFNetServiceBrowserSearchForDomains(gDomainBrowser, FALSE, NULL)) {
        
            fprintf(stdout, "Hit Ctrl-C to stop the search...\n");
            
            // Run the run loop
			CFRunLoopRun();
        }
	}
        
    // Clean up the browser if it was created.
    if (gDomainBrowser) {
        CFNetServiceBrowserInvalidate(gDomainBrowser);
        CFRelease(gDomainBrowser);
    }
    
    // Clean up the dictionary of domains if created.
    if (gDiscoveredDomains)
        CFRelease(gDiscoveredDomains);
}


/* static */ void
ServiceCallBack(CFNetServiceRef theService, CFStreamError* error, void* info) {
    
    // Unschedule the service to prevent any calls.
    CFNetServiceUnscheduleFromRunLoop(theService, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
    
    // Stop running if there was an error
    if (error && error->error) {
        CFRelease(theService);
        CFRunLoopStop(CFRunLoopGetCurrent());
    }
    
    else {

        CFReadStreamRef rStream;
        CFWriteStreamRef wStream;

        CFSocketSignature sig = {
            PF_INET,
            SOCK_STREAM,
            IPPROTO_TCP,
            CFArrayGetValueAtIndex(CFNetServiceGetAddressing(theService), 0)
        };
        
        // Create the streams.
        CFStreamCreatePairWithPeerSocketSignature(kCFAllocatorDefault, &sig, &rStream, &wStream);
        
        // Kill the run loop if failed.
        if (!rStream || !wStream)
            CFRunLoopStop(CFRunLoopGetCurrent());
        
        // Let the echo client run
        else {
            
            // Inform the streams to kill the socket when it is done with it.
            // This effects the write stream too since the pair shares the
            // one socket.
            CFReadStreamSetProperty(rStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
            
            RunEchoOnStreams(rStream, wStream);
        }
    }
}


/* static */ void
RunEchoOnStreams(CFReadStreamRef rStream, CFWriteStreamRef wStream) {

    // Try to open the streams.
    if (CFReadStreamOpen(rStream) && CFWriteStreamOpen(wStream)) {
    
        char buffer[4096];		// User input buffer.  4K should be enough.
        
        fprintf(stdout, "Connected.  Hit Ctrl-C to quit.\n");
        
        do {
            CFStreamStatus rStatus = CFReadStreamGetStatus(rStream);
            CFStreamStatus wStatus = CFWriteStreamGetStatus(wStream);
            
            CFIndex sent, rcvd, length;
        
            // Continue pumping along while waiting to open
            if ((rStatus == kCFStreamStatusOpening) || (wStatus == kCFStreamStatusOpening))
                continue;
            
            if (((rStatus == kCFStreamStatusError) || (wStatus == kCFStreamStatusError)) ||
                ((rStatus == kCFStreamStatusClosed) || (wStatus == kCFStreamStatusClosed)) ||
                ((rStatus == kCFStreamStatusAtEnd) || (wStatus == kCFStreamStatusAtEnd)))
            {
                break;
            }
            
            // Read characters off stdin.
            length = 0;
            do {
                
                char c = getchar();
                
                if (c == EOF)
                    continue;
                
                // This has the unfortunate side effect that prevents the auto-detection
                // that the streams go dead.  The dead streams won't be realized until
                // the write tries to happen.
                buffer[length] = c;
                
                // Keep going.
                length++;
            }
            while (buffer[length - 1] != '\n');

            // Start off with nothing
            sent = rcvd = 0;
            
            // Keep trying to send the data
            while (sent < length) {
            
                // Try to send
                CFIndex bytesSent = CFWriteStreamWrite(wStream, buffer, length - sent);
                
                // Check to see if an error occurred
                if (bytesSent <= 0)
                    break;
                    
                sent += bytesSent;
            }
            
            // Keep trying to receive the data
            while (rcvd < length) {
            
                // Try to send
                CFIndex bytesRead = CFReadStreamRead(rStream, &buffer[rcvd], sizeof(buffer) - rcvd);
                
                // Check to see if an error occurred
                if (bytesRead <= 0)
                    break;
                    
                rcvd += bytesRead;
            }
            
            // Terminate the received string
            buffer[rcvd] = '\0';
            
            // Print it out for the user
            fprintf(stdout, buffer);
            
        } while (1);
        
        // Close the streams
        CFReadStreamClose(rStream);
        CFWriteStreamClose(wStream);
    }
    
    CFRelease(rStream);
    CFRelease(wStream);
    
    // End the application.
    CFRunLoopStop(CFRunLoopGetCurrent());
}


/* static */ void
ConnectAndUseService(const char* nameAndDomain) {

    CFStringRef name, domain;
    
    // Try to find out where the domain starts and the name ends.  If the
    // name has a period in it, it will break this very low-tech solution.
    const char* dot = strchr(nameAndDomain, '.');
    
    // Bail if not found.
    if (!dot) return;
    
    // Create the name
    name = CFStringCreateWithBytes(kCFAllocatorDefault,
                                   nameAndDomain,
                                   dot - nameAndDomain,
                                   kCFStringEncodingUTF8,
                                   FALSE);
                                   
    dot++;
    
    // Create the domain
    domain = CFStringCreateWithBytes(kCFAllocatorDefault,
                                     dot,
                                     strlen(dot),
                                     kCFStringEncodingUTF8,
                                     FALSE);
                                     
    // Make sure both worked.
    if (name && domain) {
    
        // Create the service
        CFNetServiceRef service = CFNetServiceCreate(kCFAllocatorDefault, domain, kServiceType, name, 0);
        
        // Make sure it worked
        if (service) {
            CFNetServiceClientContext c = {0, NULL, NULL, NULL, NULL};
            
            // Set the client.  Don't need anything special in the context for this.
            if (CFNetServiceSetClient(service, ServiceCallBack, &c)) {
                
                // Schedule
                CFNetServiceScheduleWithRunLoop(service, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
                
                // Start the resolve.  Don't care about the error  
                if (CFNetServiceResolve(service, NULL))
                    CFRunLoopRun();
            }
        }
    }
}


#pragma mark -

int
main(int argc, char* argv[]) {

    // One arg means to do the search.
    if (argc == 1)
        BrowseForServers();

    // Only take a single service and no options
    else if ((argc == 2) && (argv[1][0] != '-'))
        ConnectAndUseService(argv[1]);
    
    // Show usage on the rest
    else {
    
        // Pick up the command name
        const char* start = strrchr(argv[0], '/');
        if (!start)
            start = argv[0];
        else
            start++;

        // Print usage
        fprintf(stderr, "Usage: %s [service]\n", start);
    }
    
    return 0;
}