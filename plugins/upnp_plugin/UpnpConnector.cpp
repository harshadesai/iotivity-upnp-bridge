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

#include <string.h>
#include <iostream>
#include <future>
#include <glib.h>
#include <glib-object.h>
#include <gssdp.h>
#include <gupnp.h>
#include <soup.h>
#include <boost/regex.hpp>

#include <UpnpConstants.h>

#include "UpnpConnector.h"
#include "UpnpException.h"
#include "UpnpInternal.h"
#include "UpnpRequest.h"

#include <octypes.h>
#include <ocstack.h>
#include <oic_malloc.h>
#include <oic_string.h>
#include <mpmErrorCode.h>
#include <pluginServer.h>
#include <ConcurrentIotivityUtils.h>

using namespace std;
using namespace boost;
using namespace OC::Bridging;

static const string MODULE = "UpnpConnector";

// static is necessary for callbacks defined with the c gupnp functions (c code)
static UpnpConnector::DiscoveryCallback s_discoveryCallback;
static UpnpConnector::LostCallback s_lostCallback;

static GMainLoop *s_mainLoop;
static GMainContext *s_mainContext;
static GUPnPContextManager *s_contextManager;
static UpnpManager *s_manager;
static UpnpRequestState s_requestState;
static map <gulong, GUPnPControlPoint *> s_signalMap;

static bool isRootDiscovery[] = {false, true};

const uint LIGHT_CALLBACK = 0;
const uint BINARY_SWITCH_CALLBACK = 1;
const uint BRIGHTNESS_CALLBACK = 2;
const uint AUDIO_CALLBACK = 3;
const uint MEDIA_CONTROL_CALLBACK = 4;

const uint GENERIC_DEVICE_CALLBACK = 1000;
const uint GENERIC_SERVICE_CALLBACK = 1001;
const uint GENERIC_ACTION_CALLBACK = 1002;
const uint GENERIC_STATE_VAR_CALLBACK = 1003;

UpnpConnector::UpnpConnector(DiscoveryCallback discoveryCallback, LostCallback lostCallback)
{
    DEBUG_PRINT("");

    m_discoveryCallback = discoveryCallback;
    m_lostCallback = lostCallback;

    s_discoveryCallback = m_discoveryCallback;
    s_lostCallback = m_lostCallback;
    s_manager = new UpnpManager();
    s_signalMap.clear();
}

UpnpConnector::~UpnpConnector()
{
    DEBUG_PRINT("");
    s_signalMap.clear();
    delete s_manager;
    s_manager = NULL;
}

UpnpManager* UpnpConnector::getUpnpManager()
{
    return s_manager;
}

void UpnpConnector::disconnect()
{
    DEBUG_PRINT("");

    if (!s_mainLoop)
    {
        DEBUG_PRINT("Already disconnected");
        return;
    }

    std::promise< bool > promise;
    UpnpRequest request;
    request.done = 0;
    request.expected = 0;
    {
        std::lock_guard< std::mutex > lock(s_requestState.queueLock);
        request.start = [&] ()
        {
            s_manager->stop();
            gupnpStop();
            return true;
        };
        request.finish = [&] (bool status) {(void) status; promise.set_value(true); };
        s_requestState.requestQueue.push(&request);

        if (s_requestState.sourceId == 0)
        {
            s_requestState.sourceId = g_source_attach(s_requestState.source, s_mainContext);
            DEBUG_PRINT("sourceId: " << s_requestState.sourceId << "context: " << s_mainContext);
        }

    }
    promise.get_future().get();
}

void UpnpConnector::gupnpStop()
{
    DEBUG_PRINT("");
    g_source_unref(s_requestState.source);
    g_source_destroy(s_requestState.source);
    s_requestState.sourceId = 0;

    for (auto it : s_signalMap)
    {
        g_signal_handler_disconnect (it.second, it.first);
    }

    g_object_unref(s_contextManager);
    g_main_loop_quit(s_mainLoop);
    g_main_loop_unref(s_mainLoop);
    s_mainLoop = NULL;
}

void UpnpConnector::connect()
{
    std::thread discoveryThread(&UpnpConnector::gupnpStart, this);
    discoveryThread.detach();
}

void UpnpConnector::startDiscovery(GUPnPControlPoint *controlPoint)
{
    gulong instanceId;

    // the 'device-proxy-unavailable' signal is sent when any devices are lost
    instanceId = g_signal_connect(controlPoint, "device-proxy-unavailable",
                                  G_CALLBACK (&UpnpConnector::onDeviceProxyUnavailable), NULL);
    s_signalMap[instanceId] = controlPoint;

    // the 'device-proxy-available' signal is sent when any devices are found
    instanceId = g_signal_connect(controlPoint, "device-proxy-available",
                                  G_CALLBACK (&UpnpConnector::onDeviceProxyAvailable), (gpointer *) (&isRootDiscovery[0]));
    s_signalMap[instanceId] = controlPoint;

    // the 'service-proxy-unavailable' signal is sent when any services are lost
    instanceId = g_signal_connect(controlPoint, "service-proxy-unavailable",
                                  G_CALLBACK (&UpnpConnector::onServiceProxyUnavailable), NULL);
    s_signalMap[instanceId] = controlPoint;

    // the 'service-proxy-available' signal is sent when any services are found
    instanceId = g_signal_connect(controlPoint, "service-proxy-available",
                                  G_CALLBACK (&UpnpConnector::onServiceProxyAvailable), NULL);
    s_signalMap[instanceId] = controlPoint;

    // tell the control point to start searching
    gssdp_resource_browser_set_active(GSSDP_RESOURCE_BROWSER(controlPoint), true);
}

void UpnpConnector::gupnpStart()
{
    DEBUG_PRINT("");
    if (s_mainLoop)
    {
        DEBUG_PRINT("Don't start UPnP discovery twice!");
        return;
    }

    // create a new gupnp context manager
    s_contextManager = gupnp_context_manager_create(0);

    g_signal_connect(s_contextManager, "context-available",
                     G_CALLBACK(&UpnpConnector::onContextAvailable), NULL);

    DEBUG_PRINT("UPnP main loop starting... (" << std::this_thread::get_id() << ")");
    s_mainLoop = g_main_loop_new(NULL, false);
    s_mainContext = g_main_context_default();
    DEBUG_PRINT("main context" << s_mainContext);

    s_requestState.context = s_mainContext;
    initResourceCallbackHandler();
    g_main_loop_run(s_mainLoop);
}

int UpnpConnector::checkRequestQueue(gpointer data)
{
    (void) data;
    // Check request queue
    std::lock_guard< std::mutex > lock(s_requestState.queueLock);
    DEBUG_PRINT("(" << s_requestState.requestQueue.size() << ")");

    while (!s_requestState.requestQueue.empty())
    {
        UpnpRequest *request = s_requestState.requestQueue.front();
        bool status = request->start();

        // If request completed, finalize here
        if (request->done == request->expected)
        {
            DEBUG_PRINT("finish " << request);
            request->finish(status);
        }
        s_requestState.requestQueue.pop();
    }

    DEBUG_PRINT("sourceId: " << s_requestState.sourceId << ", context: " << g_source_get_context (
                    s_requestState.source));
    if (s_requestState.sourceId != 0)
    {
        g_source_unref(s_requestState.source);
        g_source_destroy(s_requestState.source);
        s_requestState.sourceId = 0;

        // Prepare for the next scheduling call
        initResourceCallbackHandler();
    }
    return G_SOURCE_REMOVE;
}

void UpnpConnector::initResourceCallbackHandler()
{
    s_requestState.source = g_idle_source_new();
    g_source_set_priority(s_requestState.source, G_PRIORITY_HIGH_IDLE);
    g_source_set_callback(s_requestState.source,
                          checkRequestQueue,
                          NULL,
                          NULL);
}

// Callback: a gupnp context is available
void UpnpConnector::onContextAvailable(GUPnPContextManager *manager, GUPnPContext *context)
{
    GUPnPControlPoint * controlPointRoot;
    GUPnPControlPoint * controlPointAll;
    gulong instanceId;

    DEBUG_PRINT("context: " << context << ", manager: " << manager);

    // create a control point for root devices
    controlPointRoot = gupnp_control_point_new(context, "upnp:rootdevice");

    // the 'device-proxy-available' signal is sent when any devices are found
    instanceId = g_signal_connect(controlPointRoot, "device-proxy-available",
                                  G_CALLBACK (&UpnpConnector::onDeviceProxyAvailable), (gpointer *) (&isRootDiscovery[1]));
    s_signalMap[instanceId] = controlPointRoot;

    // let the context manager take care of this control point's life cycle
    gupnp_context_manager_manage_control_point(manager, controlPointRoot);
    g_object_unref(controlPointRoot);

    // create a control point (for all devices and services)
    controlPointAll = gupnp_control_point_new(context, "ssdp:all");
    startDiscovery(controlPointAll);

    // let the context manager take care of this control point's life cycle
    gupnp_context_manager_manage_control_point(manager, controlPointAll);
    g_object_unref(controlPointAll);
}

// Callback: a device has been discovered
void UpnpConnector::onDeviceProxyAvailable(GUPnPControlPoint *controlPoint,
                                           GUPnPDeviceProxy *proxy,
                                           gpointer userData)
{
    (void) controlPoint;
    GUPnPDeviceInfo *deviceInfo = GUPNP_DEVICE_INFO(proxy);
    UpnpResource::Ptr pUpnpResource;
    const string udn = gupnp_device_info_get_udn(deviceInfo);
    bool isRoot = *(static_cast <bool *> (userData));

    DEBUG_PRINT("Device type: " << gupnp_device_info_get_device_type(deviceInfo));

    // If not a light then use Generic UPnP Data Model
    if (! boost::regex_match(gupnp_device_info_get_device_type(deviceInfo), boost::regex(".*[Ll]ight.*")))
    {
        DEBUG_PRINT("Device type " << gupnp_device_info_get_device_type(deviceInfo) << " implemented as generic upnp device");
    }

#ifndef NDEBUG
    char *devModel = gupnp_device_info_get_model_name(deviceInfo);
    if (devModel != NULL)
    {
        DEBUG_PRINT("\tDevice model: " << devModel);
        g_free(devModel);
    }

    char *devName = gupnp_device_info_get_friendly_name(deviceInfo);
    if (devName != NULL)
    {
        DEBUG_PRINT("\tFriendly name: " << devName);
        g_free(devName);
    }
#endif
    DEBUG_PRINT("\tUdn: " << udn);

    if (isRoot)
    {
        // Root device
        pUpnpResource = s_manager->processDevice(proxy, deviceInfo, true, &s_requestState);
    }
    else
    {
        pUpnpResource = s_manager->processDevice(proxy, deviceInfo, false, &s_requestState);
    }

    if (pUpnpResource != nullptr && !pUpnpResource->isRegistered())
    {
        DEBUG_PRINT("Register device resource: " << pUpnpResource->m_uri);
        if (s_discoveryCallback(pUpnpResource) == 0)
        {
            pUpnpResource->setRegistered(true);
        }
        else
        {
            pUpnpResource->setRegistered(false);
            unregisterDeviceResource(udn);
            return;
        }

        // Traverse the service list and register all the services where isReady() returns true.
        // This is done in order to catch all the services that have been seen
        // prior to discovering the hosting device.
        GList *childService = gupnp_device_info_list_services (deviceInfo);

        while (childService)
        {
            GUPnPServiceInfo *serviceInfo = GUPNP_SERVICE_INFO (childService->data);
            std::shared_ptr<UpnpResource> pUpnpResourceService = s_manager->findResource(serviceInfo);
            if (pUpnpResourceService == nullptr)
            {
                DEBUG_PRINT("Registering device: Service link is empty!");
                // This could happen if support for the service is not implemented
            }
            else
            {
                DEBUG_PRINT(pUpnpResourceService->m_uri << "ready " << pUpnpResourceService->isReady() <<
                            " and registered " << pUpnpResourceService->isRegistered());
                if (pUpnpResourceService->isReady() && !pUpnpResourceService->isRegistered())
                {
                    DEBUG_PRINT("Register resource for previously discovered child service: " <<
                                pUpnpResourceService->m_uri);
                    s_discoveryCallback(pUpnpResourceService);
                    pUpnpResourceService->setRegistered(true);

                    // Subscribe to notifications
                    // Important!!! UpnpService object associated with this info/proxy
                    // must stay valid until we unsubscribe from notificatons. This
                    // means we have to keep a reference to the object inside the
                    // UpnpManager as long as we are subscribed to notifications.
                    gupnp_service_proxy_set_subscribed(GUPNP_SERVICE_PROXY(serviceInfo), true);
                }
            }
            g_object_unref (childService->data);
            childService = g_list_delete_link (childService, childService);
        }
    }
}

void UpnpConnector::onServiceProxyAvailable(GUPnPControlPoint *controlPoint,
        GUPnPServiceProxy *proxy)
{
    (void) controlPoint;
    GUPnPServiceInfo *info = GUPNP_SERVICE_INFO(proxy);

    DEBUG_PRINT("Service type: " << gupnp_service_info_get_service_type(info));
    DEBUG_PRINT("\tUdn: " << gupnp_service_info_get_udn(info));

    // Get service introspection.
    // TODO: consider using gupnp_service_info_get_introspection_full with GCancellable.
    gupnp_service_info_get_introspection_async (info,
            onIntrospectionAvailable,
            NULL);
}

// Introspection callback
void UpnpConnector::onIntrospectionAvailable(GUPnPServiceInfo          *info,
        GUPnPServiceIntrospection *introspection,
        const GError              *error,
        gpointer                  context)
{
    (void) context;
    DEBUG_PRINT(gupnp_service_info_get_service_type(info) << ", udn: " << gupnp_service_info_get_udn(
                    info));

    if (error)
    {
        ERROR_PRINT(error->message);
        return;
    }

    UpnpResource::Ptr pUpnpResourceService = nullptr;
    pUpnpResourceService = s_manager->processService(GUPNP_SERVICE_PROXY (info), info, introspection, &s_requestState);

    if (introspection != NULL)
    {
        g_object_unref(introspection);
    }

    if (pUpnpResourceService == nullptr || pUpnpResourceService->isRegistered())
    {
        return;
    }

    // Check if the service resource is ready to be registered, i.e.,
    // the resource for the hosting device has been registered.
    // If yes, proceed registering the service resource.
    // Otherwise, the registration will happen when the hosting device proxy becomes available.
    UpnpResource::Ptr pUpnpResourceDevice = s_manager->findDevice(pUpnpResourceService->getUdn());
    if ((pUpnpResourceDevice != nullptr) && pUpnpResourceDevice->isRegistered())
    {
        s_discoveryCallback(pUpnpResourceService);
        pUpnpResourceService->setRegistered(true);

        // Subscribe to notifications
        // Important!!! UpnpService object associated with this info/proxy
        // must stay valid until we unsubscribe from notificatons. This
        // means we have to keep a reference to the object inside the
        // UpnpManager as long as we are subscribed to notifications.
        gupnp_service_proxy_set_subscribed(GUPNP_SERVICE_PROXY(info), true);
    }
}

// This is a recursive call. Should be safe as the depth of a UPNP device
// tree rarely is going to exceed 3 levels. If we ever discover that
// this is not the case, unravel the call into iterative function.
void UpnpConnector::unregisterDeviceResource(string udn)
{
    std::shared_ptr<UpnpDevice> pDevice = s_manager->findDevice(udn);

    if (pDevice == nullptr)
    {
        return;
    }

    // Unregister resources and remove references for embedded services
    for (auto serviceID : pDevice->getServiceList())
    {
        string serviceKey = udn + serviceID;
        std::shared_ptr<UpnpService> pService = s_manager->findService(serviceKey);

        if (pService != nullptr)
        {
            // Unsubscribe from notifications
            if (pService->getProxy() != nullptr)
            {
                gupnp_service_proxy_set_subscribed(pService->getProxy(), false);
            }

            if (pService->isRegistered())
            {
                // Deregister service resource
                s_lostCallback(pService);
            }
        }
        s_manager->removeService(serviceKey);

    }

    // Unregister resources and remove references for embedded devices
    for (auto udnChild : pDevice->getDeviceList())
    {
        unregisterDeviceResource(udnChild);
    }
    if (pDevice->isRegistered())
    {
        s_lostCallback(pDevice);
    }
    s_manager->removeDevice(udn);
}

void UpnpConnector::onDeviceProxyUnavailable(GUPnPControlPoint *controlPoint,
        GUPnPDeviceProxy *proxy)
{
    (void) controlPoint;
    GUPnPDeviceInfo *info = GUPNP_DEVICE_INFO(proxy);
    const string udn = gupnp_device_info_get_udn(info);

    DEBUG_PRINT(": " << gupnp_device_info_get_device_type(info));
    DEBUG_PRINT("\tUdn: " << udn);

    unregisterDeviceResource(udn);
}

void UpnpConnector::onServiceProxyUnavailable(GUPnPControlPoint *controlPoint,
        GUPnPServiceProxy *proxy)
{
    (void) controlPoint;
    GUPnPServiceInfo *info = GUPNP_SERVICE_INFO(proxy);

    DEBUG_PRINT("Service type: " << gupnp_service_info_get_service_type(info));
    DEBUG_PRINT("\tUdn: " << gupnp_service_info_get_udn(info));
    UpnpResource::Ptr pUpnpResourceService = s_manager->findResource(info);

    if (pUpnpResourceService != nullptr)
    {
        // Unsubscribe from notifications
        gupnp_service_proxy_set_subscribed(proxy, false);

        if (pUpnpResourceService->isRegistered())
        {
            s_lostCallback(pUpnpResourceService);
        }
        s_manager->removeService(info);
    }
}

void UpnpConnector::onScan()
{
    DEBUG_PRINT("");
    if (s_manager) {
        s_manager->onScan();
    }
}

/*******************************************
 * callbacks for handling the pluginAdd
 *******************************************/
bool isSecureEnvironmentSet()
{
    char *non_secure_env = getenv("NONSECURE");

    if (non_secure_env != NULL && (strcmp(non_secure_env, "true") == 0))
    {
        DEBUG_PRINT("Creating NON SECURE resources");
        return false;
    }
    DEBUG_PRINT("Creating SECURE resources");
    return true;
}

OCRepPayload* OCRepPayloadCreate()
{
    OCRepPayload* payload = (OCRepPayload*)OICCalloc(1, sizeof(OCRepPayload));

    if (!payload)
    {
        return NULL;
    }

    payload->base.type = PAYLOAD_TYPE_REPRESENTATION;

    return payload;
}

OCRepPayload* getCommonPayload(const char *uri, char *interfaceQuery, string resourceType,
        OCRepPayload *payload)
{
    if (!OCRepPayloadSetUri(payload, uri))
    {
        throw "Failed to set uri in the payload";
    }

    if (!OCRepPayloadAddResourceType(payload, resourceType.c_str()))
    {
        throw "Failed to set resource type in the payload" ;
    }

    DEBUG_PRINT("Checking against if: " << interfaceQuery);

    // If the interface filter is explicitly oic.if.baseline, include all properties.
    if (interfaceQuery && string(interfaceQuery) == string(OC_RSRVD_INTERFACE_DEFAULT))
    {
        if (!OCRepPayloadAddInterface(payload, OC_RSRVD_INTERFACE_READ))
        {
            throw "Failed to set read interface";
        }

        if (!OCRepPayloadAddInterface(payload, OC_RSRVD_INTERFACE_DEFAULT))
        {
            throw "Failed to set baseline interface" ;
        }
    }
    return payload;
}

OCEntityHandlerResult handleEntityHandlerRequests( OCEntityHandlerRequest *entityHandlerRequest,
                                                   std::string resourceType)
{
    OCEntityHandlerResult ehResult = OC_EH_ERROR;
    OCRepPayload *responsePayload = NULL;
    OCRepPayload *payload = OCRepPayloadCreate();

    try
    {
        if ((entityHandlerRequest == NULL))
        {
            throw "Entity handler received a null entity request context" ;
        }

        std::string uri = OCGetResourceUri(entityHandlerRequest->resource);
        DEBUG_PRINT("URI from resource " << uri);

        bool notifyObservers = false;
        char *interfaceQuery = NULL;
        char *resourceTypeQuery = NULL;
        char *dupQuery = OICStrdup(entityHandlerRequest->query);
        if (dupQuery)
        {
            MPMExtractFiltersFromQuery(dupQuery, &interfaceQuery, &resourceTypeQuery);
        }

        for (const auto& service : s_manager->m_services)
        {
            // service entity handler does all service related resources
            // (ie uri starts with service uri)
            if (uri.find(service.second->m_uri) == 0)
            {
                switch (entityHandlerRequest->method)
                {
                    case OC_REST_GET:
                        DEBUG_PRINT(" GET Request for: " << uri);
                        ehResult = service.second->processGetRequest(uri, payload, resourceType);
                        break;

                    case OC_REST_PUT:
                    case OC_REST_POST:
                        DEBUG_PRINT("PUT / POST Request on " << uri);
                        ehResult = service.second->processPutRequest(entityHandlerRequest, uri, resourceType, payload);
                        notifyObservers = (ehResult == OC_EH_OK);
                        break;

                    default:
                        DEBUG_PRINT("UnSupported Method [" << entityHandlerRequest->method << "] Received");
                        ConcurrentIotivityUtils::respondToRequestWithError(entityHandlerRequest, " Unsupported Method", OC_EH_METHOD_NOT_ALLOWED);
                        return OC_EH_ERROR;
                }
            }
        }

        for (const auto& device : s_manager->m_devices) {
            if (device.second->m_uri == uri)
            {
                switch (entityHandlerRequest->method)
                {
                    case OC_REST_GET:
                        DEBUG_PRINT(" GET Request for: " << uri);
                        ehResult = device.second->processGetRequest(payload, resourceType);
                        break;

                    case OC_REST_PUT:
                    case OC_REST_POST:
                        DEBUG_PRINT("PUT / POST Request on " << uri << " are not supported");
                        // fall thru intentionally

                    default:
                        DEBUG_PRINT("UnSupported Method [" << entityHandlerRequest->method << "] Received");
                        ConcurrentIotivityUtils::respondToRequestWithError(entityHandlerRequest, " Unsupported Method", OC_EH_METHOD_NOT_ALLOWED);
                        return OC_EH_ERROR;
                }
            }
        }

        responsePayload = getCommonPayload(uri.c_str(), interfaceQuery, resourceType, payload);
        ConcurrentIotivityUtils::respondToRequest(entityHandlerRequest, responsePayload, ehResult);
        OICFree(dupQuery);

        if (notifyObservers)
        {
            ConcurrentIotivityUtils::queueNotifyObservers(uri);
        }
    }
    catch (const char *errorMessage)
    {
        DEBUG_PRINT("Error - " << errorMessage);
        ConcurrentIotivityUtils::respondToRequestWithError(entityHandlerRequest, errorMessage, OC_EH_ERROR);
        ehResult = OC_EH_ERROR;
    }

    OCRepPayloadDestroy(responsePayload);
    return ehResult;
}

OCEntityHandlerResult resourceEntityHandler(OCEntityHandlerFlag,
        OCEntityHandlerRequest *entityHandlerRequest,
        void *callback)
{
    DEBUG_PRINT("");
    uintptr_t callbackParamResourceType = (uintptr_t)callback;
    std::string resourceType;

    if (callbackParamResourceType == LIGHT_CALLBACK)
    {
        return handleEntityHandlerRequests(entityHandlerRequest, UPNP_OIC_TYPE_DEVICE_LIGHT);
    }
    else if (callbackParamResourceType == BINARY_SWITCH_CALLBACK)
    {
        return handleEntityHandlerRequests(entityHandlerRequest, UPNP_OIC_TYPE_POWER_SWITCH);
    }
    else if (callbackParamResourceType == BRIGHTNESS_CALLBACK)
    {
        return handleEntityHandlerRequests(entityHandlerRequest, UPNP_OIC_TYPE_BRIGHTNESS);
    }
    else if (callbackParamResourceType == AUDIO_CALLBACK)
    {
        return handleEntityHandlerRequests(entityHandlerRequest, UPNP_OIC_TYPE_AUDIO);
    }
    else if (callbackParamResourceType == MEDIA_CONTROL_CALLBACK)
    {
        return handleEntityHandlerRequests(entityHandlerRequest, UPNP_OIC_TYPE_MEDIA_CONTROL);
    }
    else if (callbackParamResourceType == GENERIC_DEVICE_CALLBACK)
    {
        return handleEntityHandlerRequests(entityHandlerRequest, UPNP_DEVICE_RESOURCE);
    }
    else if (callbackParamResourceType == GENERIC_SERVICE_CALLBACK)
    {
        return handleEntityHandlerRequests(entityHandlerRequest, UPNP_SERVICE_RESOURCE);
    }
    else if (callbackParamResourceType == GENERIC_ACTION_CALLBACK)
    {
        return handleEntityHandlerRequests(entityHandlerRequest, UPNP_ACTION_RESOURCE);
    }
    else if (callbackParamResourceType == GENERIC_STATE_VAR_CALLBACK)
    {
        return handleEntityHandlerRequests(entityHandlerRequest, UPNP_STATE_VAR_RESOURCE);
    }
    else
    {
        DEBUG_PRINT("TODO: Handle callback for " << callbackParamResourceType);
    }

    return OC_EH_ERROR;
}

void UpnpConnector::onAdd(std::string uri)
{
    DEBUG_PRINT("Adding " << uri);
    uint8_t resourceProperties = (OC_OBSERVABLE | OC_DISCOVERABLE);
    if (isSecureEnvironmentSet())
    {
        resourceProperties |= OC_SECURE;
    }

    for (const auto& service : s_manager->m_services) {
        if (service.second->m_uri == uri) {
            if (service.second->m_resourceType == UPNP_OIC_TYPE_POWER_SWITCH)
            {
                DEBUG_PRINT("Adding binary switch resource");
                createResource(uri, UPNP_OIC_TYPE_POWER_SWITCH, OC_RSRVD_INTERFACE_ACTUATOR,
                        resourceEntityHandler, (void *) BINARY_SWITCH_CALLBACK, resourceProperties);
            }
            else if (service.second->m_resourceType == UPNP_OIC_TYPE_BRIGHTNESS)
            {
                DEBUG_PRINT("Adding brightness resource");
                createResource(uri, UPNP_OIC_TYPE_BRIGHTNESS, OC_RSRVD_INTERFACE_ACTUATOR,
                        resourceEntityHandler, (void *) BRIGHTNESS_CALLBACK, resourceProperties);
            }
            else if (service.second->m_resourceType == UPNP_OIC_TYPE_AUDIO)
            {
                DEBUG_PRINT("Adding audio resource");
                createResource(uri, UPNP_OIC_TYPE_AUDIO, OC_RSRVD_INTERFACE_ACTUATOR,
                        resourceEntityHandler, (void *) AUDIO_CALLBACK, resourceProperties);
            }
            else if (service.second->m_resourceType == UPNP_OIC_TYPE_MEDIA_CONTROL)
            {
                DEBUG_PRINT("Adding media control resource");
                createResource(uri, UPNP_OIC_TYPE_MEDIA_CONTROL, OC_RSRVD_INTERFACE_ACTUATOR,
                        resourceEntityHandler, (void *) MEDIA_CONTROL_CALLBACK, resourceProperties);
            }
            else
            {
                DEBUG_PRINT("Adding generic upnp service");
                createResource(uri, UPNP_SERVICE_RESOURCE, OC_RSRVD_INTERFACE_READ,
                        resourceEntityHandler, (void *) GENERIC_SERVICE_CALLBACK, resourceProperties);
                // create resources for links
                if (!service.second->m_links.empty())
                {
                    DEBUG_PRINT("Creating resources for links");
                    for (unsigned int i = 0; i < service.second->m_links.size(); ++i) {
                        string linkUri = service.second->m_links[i].href;
                        string linkRt = service.second->m_links[i].rt;
                        if (UPNP_ACTION_RESOURCE == linkRt)
                        {
                            createResource(linkUri, linkRt, OC_RSRVD_INTERFACE_READ_WRITE,
                                resourceEntityHandler, (void *) GENERIC_ACTION_CALLBACK, resourceProperties);
                        }
                        else if (UPNP_STATE_VAR_RESOURCE == linkRt)
                        {
                            createResource(linkUri, linkRt, OC_RSRVD_INTERFACE_READ,
                                resourceEntityHandler, (void *) GENERIC_STATE_VAR_CALLBACK, resourceProperties);
                        }
                        else
                        {
                            ERROR_PRINT("Failed to create resource for unknown type " << linkRt);
                        }
                    }
                }
            }
        }
    }

    for (const auto& device : s_manager->m_devices) {
        if (device.second->m_uri == uri) {
            if (device.second->m_resourceType == UPNP_OIC_TYPE_DEVICE_LIGHT)
            {
                DEBUG_PRINT("Adding light device");
                createResource(uri, UPNP_OIC_TYPE_DEVICE_LIGHT, OC_RSRVD_INTERFACE_READ,
                        resourceEntityHandler, (void *) LIGHT_CALLBACK, resourceProperties);
            }
            else
            {
                DEBUG_PRINT("Adding generic upnp device");
                createResource(uri, UPNP_DEVICE_RESOURCE, OC_RSRVD_INTERFACE_READ,
                        resourceEntityHandler, (void *) GENERIC_DEVICE_CALLBACK, resourceProperties);
            }
        }
    }
}

OCStackResult UpnpConnector::createResource(const string uri, const string resourceTypeName,
        const char *resourceInterfaceName, OCEntityHandler resourceEntityHandler,
        void* callbackParam, uint8_t resourceProperties)
{
    OCStackResult result = OC_STACK_ERROR;
    OCResourceHandle handle = OCGetResourceHandleAtUri(uri.c_str());

    if (!handle)
    {
        result = OCCreateResource(&handle, resourceTypeName.c_str(), resourceInterfaceName,
                uri.c_str(), resourceEntityHandler, callbackParam, resourceProperties);
        if (result == OC_STACK_OK)
        {
            DEBUG_PRINT("Created resource " << uri);
            // Commented-out for now... causes CTT failure
//            result = OCBindResourceTypeToResource(handle, "oic.d.virtual");
//            if (result == OC_STACK_OK)
//            {
//                DEBUG_PRINT("Bound virtual resource type to " << uri);
//            }
//            else
//            {
//                DEBUG_PRINT("Failed to bind virtual resource type to " << uri);
//            }
            if (UPNP_OIC_TYPE_DEVICE_LIGHT == resourceTypeName ||
                UPNP_DEVICE_RESOURCE == resourceTypeName || UPNP_SERVICE_RESOURCE == resourceTypeName) {
                result = OCBindResourceTypeToResource(handle, OC_RSRVD_RESOURCE_TYPE_COLLECTION);
                if (result == OC_STACK_OK)
                {
                    DEBUG_PRINT("Bound collection resource type to " << uri);
                }
                else
                {
                    DEBUG_PRINT("Failed to bind collection resource type to " << uri);
                }

                result = OCBindResourceInterfaceToResource(handle, OC_RSRVD_INTERFACE_LL);
                if (result == OC_STACK_OK)
                {
                    DEBUG_PRINT("Bound ll resource interface to " << uri);
                }
                else
                {
                    DEBUG_PRINT("Failed to bind ll resource interface to " << uri);
                }
            }
        }
        else
        {
            DEBUG_PRINT("Failed to create resource " << uri << " result = " << result);
        }
    }
    else
    {
        DEBUG_PRINT("Not creating resource " << uri << " (already exists)");
        result = OC_STACK_OK;
    }

    return result;
}

void UpnpConnector::onRemove(std::string uri)
{
    DEBUG_PRINT("Removing " << uri);

    for (const auto& service : s_manager->m_services) {
        if (service.second->m_uri == uri) {
            if (!service.second->m_links.empty())
            {
                vector<_link> links = service.second->m_links;
                for (unsigned int i = 0; i < links.size(); ++i) {
                    OCStackResult result = OC::Bridging::ConcurrentIotivityUtils::queueDeleteResource(links[i].href);
                    DEBUG_PRINT("Service link queueDeleteResource(" << links[i].href << ") result = " << result);
                    ConcurrentIotivityUtils::queueNotifyObservers(links[i].href);
                }
            }

            OCStackResult result = OC::Bridging::ConcurrentIotivityUtils::queueDeleteResource(uri);
            DEBUG_PRINT("Service queueDeleteResource(" << uri << ") result = " << result);
            ConcurrentIotivityUtils::queueNotifyObservers(uri);
        }
    }

    for (const auto& device : s_manager->m_devices) {
        if (device.second->m_uri == uri) {
            if (!device.second->m_links.empty())
            {
                vector<_link> links = device.second->m_links;
                for (unsigned int i = 0; i < links.size(); ++i) {
                    if (links[i].rt.find(UPNP_DEVICE_RESOURCE) == 0)
                    {
                        OCStackResult result = OC::Bridging::ConcurrentIotivityUtils::queueDeleteResource(links[i].href);
                        DEBUG_PRINT("Device link queueDeleteResource(" << links[i].href << ") result = " << result);
                        ConcurrentIotivityUtils::queueNotifyObservers(links[i].href);
                    }
                }
            }

            OCStackResult result = OC::Bridging::ConcurrentIotivityUtils::queueDeleteResource(uri);
            DEBUG_PRINT("Device queueDeleteResource(" << uri << ") result = " << result);
            ConcurrentIotivityUtils::queueNotifyObservers(uri);
        }
    }
}
