#include "portal.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

#include "config.h"
#include "DEV_Config.h"

namespace {

const char *AP_SSID = "molly-count-Setup";

const char PAGE_FORM[] PROGMEM =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>molly-count</title></head>"
    "<body style='font-family:sans-serif;max-width:24em;margin:2em auto'>"
    "<h3>molly-count \u914d\u7f51</h3>"
    "<p>\u70b9\u51fb\u626b\u63cf\u5230\u7684 WiFi\uff08\u4ec5 2.4GHz\uff09\uff0c\u6216\u76f4\u63a5\u8f93\u5165\uff1a</p>"
    "<ul id=l style='padding-left:1em;min-height:1em'><li>\u626b\u63cf\u4e2d\u2026</li></ul>"
    "<form method=POST action=/save>"
    "<p><input name=ssid placeholder='WiFi \u540d\u79f0' required></p>"
    "<p><input name=pass type=password placeholder='WiFi \u5bc6\u7801'></p>"
    "<p><button style='width:100%;padding:0.6em'>\u4fdd\u5b58\u5e76\u8fde\u63a5</button></p>"
    "</form>"
    "<script>"
    "function pick(a){document.forms[0].ssid.value=a.textContent;return false;}"
    "async function s(){try{const r=await fetch('/scan');const j=await r.json();"
    "const l=document.getElementById('l');l.innerHTML='';"
    "j.forEach(n=>{const li=document.createElement('li');const a=document.createElement('a');"
    "a.href='#';a.textContent=n;a.onclick=()=>pick(a);li.appendChild(a);l.appendChild(li);});"
    "}catch(e){}setTimeout(s,3000);}s();"
    "</script></body></html>";

const char PAGE_SAVED[] PROGMEM =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "</head><body style='font-family:sans-serif;max-width:24em;margin:2em auto'>"
    "<h3>\u5df2\u4fdd\u5b58\uff0c\u8bbe\u5907\u6b63\u5728\u8fde\u63a5\u2026</h3>"
    "<p>\u53ef\u4ee5\u5173\u95ed\u6b64\u9875\u9762\u4e86\u3002</p></body></html>";

String url_decode(const String &s)
{
    String out;
    out.reserve(s.length());
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.length()) {
            out += (char)strtol(s.substring(i + 1, i + 3).c_str(), nullptr, 16);
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

String form_field(const String &body, const char *key)
{
    String k = String(key) + "=";
    int start = body.indexOf(k);
    if (start < 0) return "";
    start += k.length();
    int end = body.indexOf('&', start);
    if (end < 0) end = body.length();
    return url_decode(body.substring(start, end));
}

void serve(WiFiClient &client, const char *content_type, const char *body_progmem)
{
    String head = "HTTP/1.1 200 OK\r\nContent-Type: " + String(content_type) + "\r\n\r\n";
    client.print(head);
    char buf[512];
    size_t n;
    while ((n = strlen_P(body_progmem)) > 0) {
        if (n > sizeof buf) n = sizeof buf;
        memcpy_P(buf, body_progmem, n);
        client.write((uint8_t *)buf, n);
        body_progmem += n;
    }
    client.stop();
}

} // namespace

bool portal_run(unsigned long timeout_ms)
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA); // AP_STA so we can scan while the AP is up
    WiFi.softAP(AP_SSID);   // open AP: phones auto-prompt the captive portal
    delay(100);
    WiFi.scanNetworks(true); // kick off an async scan for the form's list

    DNSServer dns;
    dns.start(53, "*", WiFi.softAPIP());

    WiFiServer server(80);
    server.begin();

    bool saved = false;
    unsigned long start = millis();
    while (millis() - start < timeout_ms && !saved) {
        dns.processNextRequest();

        // distinct LED pattern while the portal waits: three 80ms flashes
        // then a long dark gap, repeating — recognisable at a glance as
        // "provisioning", unlike the console slow-blink or panel fast-blink
        {
            unsigned long t = millis() % 1500;
            bool on = t < 80 || (t >= 160 && t < 240) || (t >= 320 && t < 400);
            digitalWrite(EPD_LED_PIN, on ? LOW : HIGH); // low-active
        }

        WiFiClient client = server.available();
        if (!client) {
            delay(10);
            continue;
        }
        client.setTimeout(2000);

        // read the request head
        String req;
        unsigned long t0 = millis();
        while (client.connected() && millis() - t0 < 3000) {
            if (client.available()) {
                req += (char)client.read();
                if (req.length() > 2048) break;
                if (req.endsWith("\r\n\r\n")) break;
            } else {
                delay(1);
            }
        }
        if (req.length() == 0) {
            client.stop();
            continue;
        }

        bool is_post = req.startsWith("POST");
        String body;
        if (is_post) {
            int cl = req.indexOf("Content-Length:");
            if (cl < 0) cl = req.indexOf("content-length:");
            int len = cl >= 0 ? req.substring(cl + 15).toInt() : 0;
            if (len > 0 && len < 512) {
                t0 = millis();
                while (body.length() < (unsigned)len && client.connected() &&
                       millis() - t0 < 3000) {
                    if (client.available()) {
                        body += (char)client.read();
                    } else {
                        delay(1);
                    }
                }
            }
        }

        if (is_post) {
            String ssid = form_field(body, "ssid");
            String pass = form_field(body, "pass");
            if (ssid.length() > 0) {
                cfg::wifi_save(ssid, pass);
                serve(client, "text/html", PAGE_SAVED);
                saved = true;
            } else {
                client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
                client.stop();
            }
        } else if (req.indexOf("GET /scan") >= 0) {
            // return the last scan and kick off a fresh one for the next
            // poll (the form's JS re-fetches every 3s)
            String j = "[";
            int n = WiFi.scanComplete();
            if (n > 0) {
                int listed = 0;
                for (int i = 0; i < n && listed < 15; i++) {
                    String s = WiFi.SSID(i);
                    if (s.length() == 0) continue; // hidden network
                    if (listed++) j += ",";
                    j += "\"";
                    for (unsigned c = 0; c < s.length(); c++) {
                        char ch = s[c];
                        if (ch == '"' || ch == '\\') j += '\\';
                        j += ch;
                    }
                    j += "\"";
                }
            }
            j += "]";
            if (n >= 0 || n == -2) WiFi.scanNetworks(true);
            client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
            client.print(j);
            client.stop();
        } else {
            // any path serves the form: this is what makes captive
            // detection pop the portal on phones
            serve(client, "text/html", PAGE_FORM);
        }
    }

    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    return saved;
}
