//******************************************************************
//
// Copyright 2017 Intel Corporation All Rights Reserved.
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

#ifndef UPNP_BRIDGE_DEVICE_H_
#define UPNP_BRIDGE_DEVICE_H_

#include "UpnpResource.h"
#include "UpnpManager.h"

class UpnpBridgeDevice
{
    public:
        UpnpBridgeDevice();

        ~UpnpBridgeDevice();

        static OCEntityHandlerResult entityHandler(OCEntityHandlerFlag flag,
                OCEntityHandlerRequest *entityHandlerRequest, void *callback);

        static OCEntityHandlerResult handleEntityHandlerRequests(OCEntityHandlerFlag flag,
                OCEntityHandlerRequest *entityHandlerRequest, std::string resourceType);

        static OCEntityHandlerResult processGetRequest(std::string uri,
                std::string resType, OCRepPayload *payload);

        static OCRepPayload* getCommonPayload(const char *uri, char *interfaceQuery,
                std::string resourceType, OCRepPayload *payload);

        void addResource(UpnpResource::Ptr resource);
        void removeResource(std::string uri);

        void setUpnpManager(UpnpManager *upnpManager);
};

#endif
