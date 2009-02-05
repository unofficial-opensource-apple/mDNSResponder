/*
 * Copyright (c) 2002-2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@

    Change History (most recent first):

$Log: Mac\040OS\040Test\040Responder.c,v $
Revision 1.17  2003/08/14 02:19:54  cheshire
<rdar://problem/3375491> Split generic ResourceRecord type into two separate types: AuthRecord and CacheRecord

Revision 1.16  2003/08/12 19:56:24  cheshire
Update to APSL 2.0

 */

#include <stdio.h>						// For printf()
#include <string.h>			// For strlen() etc.

#include <Events.h>						// For WaitNextEvent()
#include <SIOUX.h>						// For SIOUXHandleOneEvent()

#include "mDNSClientAPI.h"				// Defines the interface to the client layer above

#include "mDNSMacOS9.h"					// Defines the specific types needed to run mDNS on this platform

// These don't have to be globals, but their memory does need to remain valid for as
// long as the search is going on. They are declared as globals here for simplicity.
static mDNS m;
static mDNS_PlatformSupport p;
static ServiceRecordSet p1, p2, afp, http, njp;
static AuthRecord browsedomain;

// This sample code just calls mDNS_RenameAndReregisterService to automatically pick a new
// unique name for the service. For a device such as a printer, this may be appropriate.
// For a device with a user interface, and a screen, and a keyboard, the appropriate
// response may be to prompt the user and ask them to choose a new name for the service.
mDNSlocal void Callback(mDNS *const m, ServiceRecordSet *const sr, mStatus result)
	{
	switch (result)
		{
		case mStatus_NoError:      debugf("Callback: %##s Name Registered",   sr->RR_SRV.resrec.name.c); break;
		case mStatus_NameConflict: debugf("Callback: %##s Name Conflict",     sr->RR_SRV.resrec.name.c); break;
		case mStatus_MemFree:      debugf("Callback: %##s Memory Free",       sr->RR_SRV.resrec.name.c); break;
		default:                   debugf("Callback: %##s Unknown Result %d", sr->RR_SRV.resrec.name.c, result); break;
		}

	if (result == mStatus_NameConflict) mDNS_RenameAndReregisterService(m, sr, mDNSNULL);
	}

// RegisterService() is a simple wrapper function which takes C string
// parameters, converts them to domainname parameters, and calls mDNS_RegisterService()
mDNSlocal void RegisterService(mDNS *m, ServiceRecordSet *recordset,
	UInt16 PortAsNumber, const char txtinfo[],
	const domainlabel *const n, const char type[], const char domain[])
	{
	mDNSIPPort port;
	domainname t;
	domainname d;
	char buffer[512];
	UInt8 txtbuffer[512];

	port.b[0] = (UInt8)(PortAsNumber >> 8);
	port.b[1] = (UInt8)(PortAsNumber     );
	MakeDomainNameFromDNSNameString(&t, type);
	MakeDomainNameFromDNSNameString(&d, domain);
	
	if (txtinfo)
		{
		strncpy((char*)txtbuffer+1, txtinfo, sizeof(txtbuffer)-1);
		txtbuffer[0] = (UInt8)strlen(txtinfo);
		}
	else
		txtbuffer[0] = 0;

	mDNS_RegisterService(m, recordset,
		n, &t, &d,									// Name, type, domain
		mDNSNULL, port,								// Host and port
		txtbuffer, (mDNSu16)(1+txtbuffer[0]),		// TXT data, length
		mDNSNULL, 0,								// Subtypes (none)
		mDNSInterface_Any,							// Interace ID
		Callback, mDNSNULL);						// Callback and context

	ConvertDomainNameToCString(&recordset->RR_SRV.resrec.name, buffer);
	printf("Made Service Records for %s\n", buffer);
	}

// RegisterFakeServiceForTesting() simulates the effect of services being registered on
// dynamically-allocated port numbers. No real service exists on that port -- this is just for testing.
mDNSlocal void RegisterFakeServiceForTesting(mDNS *m, ServiceRecordSet *recordset, const char txtinfo[],
	const char name[], const char type[], const char domain[])
	{
	static UInt16 NextPort = 0xF000;
	domainlabel n;
	MakeDomainLabelFromLiteralString(&n, name);
	RegisterService(m, recordset, NextPort++, txtinfo, &n, type, domain);
	}

// CreateProxyRegistrationForRealService() checks to see if the given port is currently
// in use, and if so, advertises the specified service as present on that port.
// This is useful for advertising existing real services (Personal Web Sharing, Personal
// File Sharing, etc.) that currently don't register with mDNS Service Discovery themselves.
mDNSlocal OSStatus CreateProxyRegistrationForRealService(mDNS *m, UInt16 PortAsNumber, const char txtinfo[],
	const char *servicetype, ServiceRecordSet *recordset)
	{
	mDNSIPPort port;
	InetAddress ia;
	TBind bindReq;
	OSStatus err;
	TEndpointInfo endpointinfo;
	EndpointRef ep = OTOpenEndpoint(OTCreateConfiguration(kTCPName), 0, &endpointinfo, &err);
	if (!ep || err) { printf("OTOpenEndpoint (CreateProxyRegistrationForRealService) failed %d", err); return(err); }

	port.b[0] = (UInt8)(PortAsNumber >> 8);
	port.b[1] = (UInt8)(PortAsNumber     );
	ia.fAddressType = AF_INET;
	ia.fPort        = port.NotAnInteger;
	ia.fHost        = 0;
	bindReq.addr.maxlen = sizeof(ia);
	bindReq.addr.len    = sizeof(ia);
	bindReq.addr.buf    = (UInt8*)&ia;
	bindReq.qlen        = 0;
	err = OTBind(ep, &bindReq, NULL);

	if (err == kOTBadAddressErr)
		RegisterService(m, recordset, PortAsNumber, txtinfo, &m->nicelabel, servicetype, "local.");
	else if (err)
		debugf("OTBind failed %d", err);

	OTCloseProvider(ep);
	return(noErr);
	}

// Done once on startup, and then again every time our address changes
mDNSlocal OSStatus mDNSResponderTestSetup(mDNS *m)
	{
	char buffer[256];
	mDNSv4Addr ip = m->HostInterfaces->ip.ip.v4;
	
	ConvertDomainNameToCString(&m->hostname, buffer);
	printf("Name %s\n", buffer);
	printf("IP   %d.%d.%d.%d\n", ip.b[0], ip.b[1], ip.b[2], ip.b[3]);

	printf("\n");
	printf("Registering Service Records\n");
	// Create example printer discovery records
	//static ServiceRecordSet p1, p2;

#define SRSET 0
#if SRSET==0
	RegisterFakeServiceForTesting(m, &p1, "path=/index.html", "Web Server One", "_http._tcp.", "local.");
	RegisterFakeServiceForTesting(m, &p2, "path=/path.html",  "Web Server Two", "_http._tcp.", "local.");
#elif SRSET==1
	RegisterFakeServiceForTesting(m, &p1, "rn=lpq1", "Epson Stylus 900N", "_printer._tcp.", "local.");
	RegisterFakeServiceForTesting(m, &p2, "rn=lpq2", "HP LaserJet",       "_printer._tcp.", "local.");
#else
	RegisterFakeServiceForTesting(m, &p1, "rn=lpq3", "My Printer",        "_printer._tcp.", "local.");
	RegisterFakeServiceForTesting(m, &p2, "lrn=pq4", "My Other Printer",  "_printer._tcp.", "local.");
#endif

	// If AFP Server is running, register a record for it
	CreateProxyRegistrationForRealService(m, 548, "", "_afpovertcp._tcp.", &afp);

	// If Web Server is running, register a record for it
	CreateProxyRegistrationForRealService(m, 80, "", "_http._tcp.", &http);

	// And pretend we always have an NJP server running on port 80 too
	//RegisterService(m, &njp, 80, "NJP/", &m->nicelabel, "_njp._tcp.", "local.");

	// Advertise that apple.com. is available for browsing
	mDNS_AdvertiseDomains(m, &browsedomain, mDNS_DomainTypeBrowse, mDNSInterface_Any, "IL 2\\4th Floor.apple.com.");

	return(kOTNoError);
	}

// YieldSomeTime() just cooperatively yields some time to other processes running on classic Mac OS
mDNSlocal Boolean YieldSomeTime(UInt32 milliseconds)
	{
	extern Boolean SIOUXQuitting;
	EventRecord e;
	WaitNextEvent(everyEvent, &e, milliseconds / 17, NULL);
	SIOUXHandleOneEvent(&e);
	return(SIOUXQuitting);
	}

int main()
	{
	extern void mDNSPlatformIdle(mDNS *const m);	// Only needed for debugging version
	mStatus err;
	Boolean DoneSetup = false;

	SIOUXSettings.asktosaveonclose = false;
	SIOUXSettings.userwindowtitle = "\pMulticast DNS Responder";

	printf("Prototype Multicast DNS Responder\n\n");
	printf("WARNING! This is experimental software.\n\n");
	printf("Multicast DNS is currently an experimental protocol.\n\n");
	printf("This software reports errors using MacsBug breaks,\n");
	printf("so if you don't have MacsBug installed your Mac may crash.\n\n");
	printf("******************************************************************************\n");

	err = InitOpenTransport();
	if (err) { debugf("InitOpenTransport failed %d", err); return(err); }

	err = mDNS_Init(&m, &p, mDNS_Init_NoCache, mDNS_Init_ZeroCacheSize,
		mDNS_Init_AdvertiseLocalAddresses, mDNS_Init_NoInitCallback, mDNS_Init_NoInitCallbackContext);
	if (err) return(err);

	while (!YieldSomeTime(35))
		{
		// For debugging, use "#define __ONLYSYSTEMTASK__ 1" and call mDNSPlatformIdle() periodically.
		// For shipping code, don't define __ONLYSYSTEMTASK__, and you don't need to call mDNSPlatformIdle()
		mDNSPlatformIdle(&m);	// Only needed for debugging version
		if (m.mDNSPlatformStatus == mStatus_NoError && !DoneSetup)
			{
			DoneSetup = true;
			printf("\nListening for mDNS queries...\n");
			mDNSResponderTestSetup(&m);
			}
		}
	
	if (p1.RR_SRV.resrec.RecordType  ) mDNS_DeregisterService(&m, &p1);
	if (p2.RR_SRV.resrec.RecordType  ) mDNS_DeregisterService(&m, &p2);
	if (afp.RR_SRV.resrec.RecordType ) mDNS_DeregisterService(&m, &afp);
	if (http.RR_SRV.resrec.RecordType) mDNS_DeregisterService(&m, &http);
	if (njp.RR_SRV.resrec.RecordType ) mDNS_DeregisterService(&m, &njp);

	mDNS_Close(&m);
	
	return(0);
	}
