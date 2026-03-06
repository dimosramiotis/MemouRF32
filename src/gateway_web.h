/**
 * Gateway web UI: node list, pending approvals, relay control, ping.
 * Adds routes to the existing WebServer instance.
 */

#ifndef GATEWAY_WEB_H
#define GATEWAY_WEB_H

#include <WebServer.h>

void gatewayWebSetup(WebServer& server);

#endif // GATEWAY_WEB_H
