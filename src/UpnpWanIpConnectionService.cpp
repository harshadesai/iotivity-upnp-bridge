//******************************************************************
//
// Copyright 2016 Intel Corporation All Rights Reserved.
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

#include "UpnpWanIpConnectionService.h"

using namespace OIC::Service;

static const string MODULE = "WanIpConnection";

// WAN IP Connection service

// Organization:
// Attribute Name,
// State Variable Name (can be empty string), State variable type, "evented" flag
// Actions:
//    0: "GET" action name, action type, optional out parameters: var_name,var_type
//    1: "SET" action name, action type, optional in parameters: var_name,var_type
// Vector of embedded attributes (if present)
vector <UpnpAttributeInfo> UpnpWanIpConnection::Attributes =
{
    {
        "autoDiscoTime",
        "AutoDisconnectTime", G_TYPE_UINT, false,
        {   {"GetAutoDisconnectTime", UPNP_ACTION_GET,  "NewAutoDisconnectTime", G_TYPE_UINT},
            {"SetAutoDisconnectTime", UPNP_ACTION_POST, "NewAutoDisconnectTime", G_TYPE_UINT}
        },
        {}
    },
    {
        "warnDiscoTime",
        "WarnDisconnectTime", G_TYPE_UINT, false,
        {   {"GetWarnDisconnectTime", UPNP_ACTION_GET,  "NewWarnDisconnectTime", G_TYPE_UINT},
            {"SetWarnDisconnectTime", UPNP_ACTION_POST, "NewWarnDisconnectTime", G_TYPE_UINT}
        },
        {}
    },
    {
        "idleDiscoTime",
        "IdleDisconnectTime", G_TYPE_UINT, false,
        {   {"GetIdleDisconnectTime", UPNP_ACTION_GET,  "NewIdleDisconnectTime", G_TYPE_UINT},
            {"SetIdleDisconnectTime", UPNP_ACTION_POST, "NewIdleDisconnectTime", G_TYPE_UINT}
        },
        {}
    },
    {
        "externAddr",
        "ExternalIPAddress", G_TYPE_STRING, true,
        {{"GetExternalIPAddress", UPNP_ACTION_GET, "NewExternalIPAddress", G_TYPE_STRING}},
        {}
    },
    {
        "connectionTypeInfo",
        "", G_TYPE_NONE, false,
        {   {"GetConnectionTypeInfo", UPNP_ACTION_GET,  NULL, G_TYPE_NONE},
            {"SetConnectionType",     UPNP_ACTION_POST, NULL, G_TYPE_NONE}
        },
        {   {"type",     "ConnectionType", G_TYPE_STRING, false},
            {"allTypes", "PossibleConnectionTypes", G_TYPE_BOOLEAN, false}
        }
    },
    {
        "nat",
        "", G_TYPE_NONE, false,
        {{"GetNATRSIPStatus", UPNP_ACTION_GET, NULL, G_TYPE_NONE}},
        {   {"rsip",    "RSIPAvailable", G_TYPE_BOOLEAN, false},
            {"enabled", "NATEnabled",    G_TYPE_BOOLEAN, false}
        }
    },
    // Special case: no matching UPNP action, but the attribute value
    // can be set based on observation
    {
        "updateId",
        "SystemUpdateID", G_TYPE_UINT, true,
        {{"", UPNP_ACTION_GET, "", G_TYPE_NONE}}, {}
    },
    // Special case: no matching UPNP action, but the attribute value
    // can be set based on observation
    {
        "sizePortMap",
        "PortMappingNumberOfEntries", G_TYPE_UINT, true,
        {{"", UPNP_ACTION_GET, "", G_TYPE_NONE}}, {}
    },
};

// Custom action map:
// "attribute name" -> GET request handlers
map <const string, UpnpWanIpConnection::GetAttributeHandler>
UpnpWanIpConnection::GetAttributeActionMap =
{
    {"nat", &UpnpWanIpConnection::getNatStatus},
    {"connectionTypeInfo", &UpnpWanIpConnection::getConnectionTypeInfo}
};

// Custom action map:
// "attribute name" -> SET request handlers
map <const string, UpnpWanIpConnection::SetAttributeHandler>
UpnpWanIpConnection::SetAttributeActionMap =
{
    {"connectionTypeInfo", &UpnpWanIpConnection::setConnectionTypeInfo}
};

void UpnpWanIpConnection::getNatStatusCb(GUPnPServiceProxy *proxy,
                                         GUPnPServiceProxyAction *actionProxy,
                                         gpointer userData)
{
    GError *error = NULL;
    bool rsip;
    bool natEnabled;

    UpnpRequest *request = static_cast<UpnpRequest *> (userData);

    bool status = gupnp_service_proxy_end_action (proxy,
                                                  actionProxy,
                                                  &error,
                                                  "NewRSIPAvailable",
                                                  G_TYPE_BOOLEAN,
                                                  &rsip,
                                                  "NewNATEnabled",
                                                  G_TYPE_BOOLEAN,
                                                  &natEnabled,
                                                  NULL);
    if (error)
    {
        ERROR_PRINT("GetNatStatus failed: " << error->code << ", " << error->message);
        g_error_free (error);
        status = false;
    }

    if (status)
    {
        RCSResourceAttributes nat;

        DEBUG_PRINT("rsip=" << rsip << ", enabled=" << natEnabled);
        nat["rsip"]   = rsip;
        nat["enabled"]   = natEnabled;
        request->resource->setAttribute("nat", nat, false);
    }

    UpnpRequest::requestDone(request, status);
}

bool UpnpWanIpConnection::getNatStatus(UpnpRequest *request)
{
    DEBUG_PRINT("");
    GUPnPServiceProxyAction *actionProxy = gupnp_service_proxy_begin_action (m_proxy,
                                           "GetNATRSIPStatus",
                                           getNatStatusCb,
                                           (gpointer *) request,
                                           NULL);
    if (NULL == actionProxy)
    {
        return false;
    }

    return true;
}

void UpnpWanIpConnection::getConnectionTypeInfoCb(GUPnPServiceProxy *proxy,
                                                  GUPnPServiceProxyAction *actionProxy,
                                                  gpointer userData)
{
    GError *error = NULL;
    const char *connType;
    const char *allTypes;

    UpnpRequest *request = static_cast<UpnpRequest *> (userData);

    bool status = gupnp_service_proxy_end_action (proxy,
                                                  actionProxy,
                                                  &error,
                                                  "NewConnectionType",
                                                  G_TYPE_STRING,
                                                  &connType,
                                                  "NewPossibleConnectionTypes",
                                                  G_TYPE_STRING,
                                                  &allTypes,
                                                  NULL);
    if (error)
    {
        ERROR_PRINT("GetConnectionTypeInfo failed: " << error->code << ", " << error->message);
        g_error_free (error);
        status = false;
    }

    if (status)
    {
        RCSResourceAttributes connTypeInfo;

        DEBUG_PRINT("type=" << connType << ", allTypes=" << allTypes);
        connTypeInfo["type"] = string(connType);
        connTypeInfo["allTypes"] = string(allTypes);
        request->resource->setAttribute("connectionTypeInfo", connTypeInfo, false);
    }

    UpnpRequest::requestDone(request, status);
}

bool UpnpWanIpConnection::getConnectionTypeInfo(UpnpRequest *request)
{
    DEBUG_PRINT("");
    GUPnPServiceProxyAction *actionProxy
        = gupnp_service_proxy_begin_action (m_proxy,
                                            "GetConnectionTypeInfo",
                                            getConnectionTypeInfoCb,
                                            (gpointer *) request,
                                            NULL);
    if (NULL == actionProxy)
    {
        return false;
    }

    return true;
}

void UpnpWanIpConnection::setConnectionTypeInfoCb(GUPnPServiceProxy *proxy,
                                                  GUPnPServiceProxyAction *actionProxy,
                                                  gpointer userData)
{
    GError *error = NULL;

    UpnpRequest *request = static_cast<UpnpRequest *> (userData);

    bool status = gupnp_service_proxy_end_action (proxy,
                                                  actionProxy,
                                                  &error,
                                                  NULL);
    if (error)
    {
        ERROR_PRINT("SetConnectionTypeInfo failed: " << error->code << ", " << error->message);
        g_error_free (error);
        status = false;
    }

    if (status)
    {
        const char *type;
        const char *allTypes;
        RCSResourceAttributes connTypeInfo;
        std::map< GUPnPServiceProxyAction *, std::pair <UpnpAttributeInfo *, std::vector <UpnpVar> > >::iterator
        it = request->proxyMap.find(actionProxy);

        assert(it != request->proxyMap.end());

        // Important! Values need to retrieved in the same order they
        // have been stored
        type = it->second.second[0].var_pchar;
        allTypes = it->second.second[1].var_pchar;

        DEBUG_PRINT("type=" << type << ", allTypes=" << allTypes);
        connTypeInfo["type"]   = string(type);
        connTypeInfo["allTypes"]   = string(allTypes);
        request->resource->setAttribute("connectionTypeInfo", connTypeInfo, false);
    }

    UpnpRequest::requestDone(request, status);
}

bool UpnpWanIpConnection::setConnectionTypeInfo(UpnpRequest *request,
                                                RCSResourceAttributes::Value *attrValue)
{
    DEBUG_PRINT("");
    GUPnPServiceProxyAction *actionProxy;
    const char *sNewType;
    const char *sAllTypes;
    UpnpVar value;
    int count = 0;

    const auto &attrVector = attrValue->get< vector< RCSResourceAttributes > >();
    for (const auto &attrs : attrVector)
    {
        for (const auto &kvPair : attrs)
        {
            string sValue = (kvPair.value()). get < string> ();
            if (kvPair.key() == "type")
            {
                sNewType = sValue.c_str();
                count++;
            }
            else if (kvPair.key() == "allTypes")
            {
                sAllTypes = sValue.c_str();
                count++;
            }
            else
            {
                ERROR_PRINT("Invalid attribute name:" << kvPair.key());
                return false;
            }
        }
    }

    // Check, if all the sub-attributes have been initialized.
    // In case of "connectionTypeInfo", there are 2 sub-attributes: "type" and "allTypes"
    if (count < 2)
    {
        ERROR_PRINT("Incomplete attribute");
        return false;
    }

    actionProxy = gupnp_service_proxy_begin_action (m_proxy,
                                                    "SetConnectionType",
                                                    setConnectionTypeInfoCb,
                                                    (gpointer *) request,
                                                    "NewConnectionType",
                                                    G_TYPE_STRING,
                                                    sNewType,
                                                    NULL);
    if (NULL == actionProxy)
    {
        return false;
    }

    request->proxyMap[actionProxy].first  = m_attributeMap["connectionTypeInfo"].first;

    // Important! Values need to be stored in the same order they
    // going to be retrieved
    value.var_pchar = sNewType;
    request->proxyMap[actionProxy].second.push_back(value);
    value.var_pchar = sAllTypes;
    request->proxyMap[actionProxy].second.push_back(value);

    return true;
}

bool UpnpWanIpConnection::getAttributesRequest(UpnpRequest *request,
                                               const map< string, string > &queryParams)
{
    bool status = false;

    for (auto it = m_attributeMap.begin(); it != m_attributeMap.end(); ++it)
    {
        bool result = false;
        UpnpAttributeInfo *attrInfo = it->second.first;

        DEBUG_PRINT(" \"" << it->first << "\"");
        // Check the request
        if (!UpnpAttribute::isValidRequest(&m_attributeMap, it->first, UPNP_ACTION_GET))
        {
            request->done++;
            continue;
        }

        // Check if custom GET needs to be called
        auto attr = this->GetAttributeActionMap.find(it->first);
        if (attr != this->GetAttributeActionMap.end())
        {
            GetAttributeHandler fp = attr->second;
            result = (this->*fp)(request);
        }
        else if (string(attrInfo->actions[0].name) != "")
        {
            result = UpnpAttribute::get(m_proxy, request, attrInfo);
        }

        status |= result;
        if (!result)
        {
            request->done++;
        }
    }
    return status;
}

bool UpnpWanIpConnection::setAttributesRequest(const RCSResourceAttributes &value,
                                               UpnpRequest *request,
                                               const map< string, string > &queryParams)
{
    bool status = false;

    for (auto it = value.begin(); it != value.end(); ++it)
    {
        bool result = false;
        const std::string attrName = it->key();

        DEBUG_PRINT(" \"" << attrName << "\"");

        // Check the request
        if (!UpnpAttribute::isValidRequest(&m_attributeMap, attrName, UPNP_ACTION_POST))
        {
            request->done++;
            continue;
        }
        RCSResourceAttributes::Value attrValue = it->value();

        UpnpAttributeInfo *attrInfo = m_attributeMap[attrName].first;

        // Check if custom SET needs to be called
        auto attr = this->SetAttributeActionMap.find(attrName);
        if (attr != this->SetAttributeActionMap.end())
        {
            SetAttributeHandler fp = attr->second;
            result = (this->*fp)(request, &attrValue);
        }
        else
        {
            result = UpnpAttribute::set(m_proxy, request, attrInfo, &attrValue);
        }

        status |= result;
        if (!result)
        {
            request->done++;
        }
    }

    return status;
}

bool UpnpWanIpConnection::processNotification(string attrName,
                                              string parent,
                                              GValue *value)
{

    if (attrName == "sizePortMap")
    {
        // Need to keep number of ports around
        m_sizePortMap = (int) g_value_get_uint(value);
        BundleResource::setAttribute("sizePortMap", m_sizePortMap);
        return true;
    }

    // Default: no custom variable->attribute conversion
    return false;
}