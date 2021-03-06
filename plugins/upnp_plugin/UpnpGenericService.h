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

#ifndef UPNP_GENERIC_SERVICE_H_
#define UPNP_GENERIC_SERVICE_H_

#include <string>
#include <map>

#include <gupnp.h>

#include "UpnpResource.h"
#include "UpnpInternal.h"
#include "UpnpService.h"

using namespace std;

// for state variables
static const string STATE_VAR_NAME = "name";
static const string DATA_TYPE = "dataType";
static const string DEFAULT_VALUE = "defaultValue";
static const string ALLOWED_VALUE_LIST = "allowedValueList";

// for actions
static const string ACTION_NAME = "actionName";
static const string INPUT_ARGS = "inputArgs";
static const string OUTPUT_ARGS = "outputArgs";

// for generic args
static const string GEN_NAME = "genName";
static const string GEN_TYPE = "genType";
static const string GEN_VALUE = "genValue";

struct _genArg {
    string name;
    string type;
    string value;
};

// GType to Upnp Type map
static const string UPNP_TYPE_BOOLEAN = "boolean";
static const string UPNP_TYPE_STRING = "string";
static const string UPNP_TYPE_INT = "int";
static const string UPNP_TYPE_UI4 = "ui4";

static map<string, string> GTypeToUpnpTypeMap =
{
    {"gboolean",  UPNP_TYPE_BOOLEAN},
    {"gchararray",UPNP_TYPE_STRING},
    {"gint",      UPNP_TYPE_INT},
    {"guint",     UPNP_TYPE_UI4},
};

class UpnpGenericService: public UpnpService
{
        friend class UpnpService;

    public:
        UpnpGenericService(GUPnPServiceInfo *serviceInfo, UpnpRequestState *requestState, string serviceType):
            UpnpService(serviceInfo, serviceType, requestState, &Attributes)
        {
        }

        OCEntityHandlerResult processGetRequest(string uri, OCRepPayload *payload, string resourceType);
        OCEntityHandlerResult processPutRequest(OCEntityHandlerRequest *ehRequest,
                    string uri, string resourceType, OCRepPayload *payload);

    private:
        static vector <UpnpAttributeInfo> Attributes;
};

#endif
