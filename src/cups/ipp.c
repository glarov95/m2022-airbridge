/* One IPP Get-Printer-Attributes through the system libcups, for doctor and status. */
#include "m2022/cups.h"

#include <cups/cups.h>
#include <stdio.h>
#include <string.h>

int m2022_ipp_printer_state(const char *host, int port, const char *resource,
                            m2022_ipp_printer_t *out)
{
    http_t *http;
    ipp_t *request, *response;
    ipp_attribute_t *attr;
    char uri[512];
    static const char *const wanted[] = {
        "printer-name",  "printer-uuid",          "printer-make-and-model",
        "printer-state", "printer-state-reasons", "printer-firmware-string-version"};

    memset(out, 0, sizeof *out);
    http = httpConnect2(host, port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_IF_REQUESTED, 1, 3000, NULL);
    if (http == NULL) {
        snprintf(out->reasons, sizeof out->reasons, "%s", cupsLastErrorString());
        return -1;
    }
    httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof uri, "ipp", NULL, host, port, resource);
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes",
                  (int)(sizeof wanted / sizeof wanted[0]), NULL, wanted);
    response = cupsDoRequest(http, request, resource);
    if (response == NULL || cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
        snprintf(out->reasons, sizeof out->reasons, "%s", cupsLastErrorString());
        ippDelete(response);
        httpClose(http);
        return -2;
    }
    if ((attr = ippFindAttribute(response, "printer-name", IPP_TAG_NAME)) != NULL) {
        snprintf(out->name, sizeof out->name, "%s", ippGetString(attr, 0, NULL));
    }
    if ((attr = ippFindAttribute(response, "printer-uuid", IPP_TAG_URI)) != NULL) {
        snprintf(out->uuid, sizeof out->uuid, "%s", ippGetString(attr, 0, NULL));
    }
    if ((attr = ippFindAttribute(response, "printer-make-and-model", IPP_TAG_TEXT)) != NULL) {
        snprintf(out->make_model, sizeof out->make_model, "%s", ippGetString(attr, 0, NULL));
    }
    if ((attr = ippFindAttribute(response, "printer-state", IPP_TAG_ENUM)) != NULL) {
        out->state = ippGetInteger(attr, 0);
    }
    if ((attr = ippFindAttribute(response, "printer-state-reasons", IPP_TAG_KEYWORD)) != NULL) {
        ippAttributeString(attr, out->reasons, sizeof out->reasons);
    }
    if ((attr = ippFindAttribute(response, "printer-firmware-string-version", IPP_TAG_TEXT)) !=
        NULL) {
        snprintf(out->version, sizeof out->version, "%s", ippGetString(attr, 0, NULL));
    }
    ippDelete(response);
    httpClose(http);
    return 0;
}
