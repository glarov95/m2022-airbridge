/* DNS-SD browse for the doctor: is an _ipp._tcp instance with our name on the network? */
#include "m2022/service.h"

#include <arpa/inet.h>
#include <dns_sd.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

typedef struct {
    const char *name;
    bool found;
    int port;
    char domain[256];
    uint32_t interface_index;
} browse_ctx_t;

static void resolve_cb(DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifindex,
                       DNSServiceErrorType err, const char *fullname, const char *host,
                       uint16_t port, uint16_t txt_len, const unsigned char *txt, void *ctx)
{
    browse_ctx_t *b = ctx;
    (void)ref;
    (void)flags;
    (void)ifindex;
    (void)fullname;
    (void)host;
    (void)txt_len;
    (void)txt;
    if (err == kDNSServiceErr_NoError) {
        b->port = ntohs(port);
    }
}

static void browse_cb(DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifindex,
                      DNSServiceErrorType err, const char *name, const char *regtype,
                      const char *domain, void *ctx)
{
    browse_ctx_t *b = ctx;
    (void)ref;
    (void)regtype;
    if (err == kDNSServiceErr_NoError && (flags & kDNSServiceFlagsAdd) &&
        strcmp(name, b->name) == 0 && !b->found) {
        b->found = true;
        b->interface_index = ifindex;
        snprintf(b->domain, sizeof b->domain, "%s", domain);
    }
}

/* Pump one service ref until done() or the deadline. */
static void pump(DNSServiceRef ref, const struct timeval *deadline, const bool *done)
{
    int fd = DNSServiceRefSockFD(ref);
    while (!*done) {
        struct timeval now, wait;
        fd_set fds;
        gettimeofday(&now, NULL);
        if (timercmp(&now, deadline, >=)) {
            return;
        }
        timersub(deadline, &now, &wait);
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        if (select(fd + 1, &fds, NULL, NULL, &wait) <= 0) {
            return;
        }
        if (DNSServiceProcessResult(ref) != kDNSServiceErr_NoError) {
            return;
        }
    }
}

bool m2022_dnssd_find(const char *name, unsigned timeout_ms, int *port)
{
    DNSServiceRef ref = NULL;
    browse_ctx_t ctx;
    struct timeval deadline;

    memset(&ctx, 0, sizeof ctx);
    ctx.name = name;
    ctx.port = -1;
    if (port != NULL) {
        *port = -1;
    }
    gettimeofday(&deadline, NULL);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_usec += (long)(timeout_ms % 1000) * 1000;
    if (deadline.tv_usec >= 1000000) {
        deadline.tv_sec++;
        deadline.tv_usec -= 1000000;
    }
    if (DNSServiceBrowse(&ref, 0, 0, "_ipp._tcp", NULL, browse_cb, &ctx) !=
        kDNSServiceErr_NoError) {
        return false;
    }
    pump(ref, &deadline, &ctx.found);
    DNSServiceRefDeallocate(ref);
    if (!ctx.found) {
        return false;
    }
    if (port != NULL) {
        bool resolved = false;
        DNSServiceRef rref = NULL;
        if (DNSServiceResolve(&rref, 0, ctx.interface_index, name, "_ipp._tcp", ctx.domain,
                              resolve_cb, &ctx) == kDNSServiceErr_NoError) {
            /* one result is enough; resolve_cb sets the port, pump until the deadline */
            while (ctx.port < 0) {
                struct timeval now;
                gettimeofday(&now, NULL);
                if (timercmp(&now, &deadline, >=)) {
                    break;
                }
                pump(rref, &deadline, &resolved); /* returns on deadline or error */
                if (ctx.port < 0) {
                    break;
                }
            }
            DNSServiceRefDeallocate(rref);
        }
        *port = ctx.port;
    }
    return true;
}
