#pragma once
// Copy this file to  secrets.h  and fill in your values.
// secrets.h is git-ignored so your credentials never get published.

#define WIFI_SSID  "your-wifi-ssid"
#define WIFI_PASS  "your-wifi-password"

// Friendly name: shows in your router and at http://<HOSTNAME>.local
#define HOSTNAME   "vevor-fan-hub"

// Fixed IP so Home Assistant always finds it. Set USE_STATIC_IP 0 for DHCP.
// Best practice: an address OUTSIDE your router's DHCP pool (or add a DHCP reservation).
#define USE_STATIC_IP 1
#define STATIC_IP   192,168,1,30
#define GATEWAY_IP  192,168,1,1
#define SUBNET_MASK 255,255,255,0
#define DNS_IP      192,168,1,1
