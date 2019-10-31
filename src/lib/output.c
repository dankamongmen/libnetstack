#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "netstack.h"

static inline
const char* family_to_str(unsigned family){
  switch(family){
    case AF_INET6: return "IPv6";
    case AF_INET: return "IPv4";
    default: return "unknown family";
  }
}

// Each byte becomes two ASCII characters + separator or nul
static inline size_t
hwaddrstrlen(size_t addrlen){
  return ((addrlen) == 0 ? 1 : (addrlen == 1) ? 2 : (addrlen) * 3);
}

// buf must be at least slen bytes
void* l2ntop(const void *hwaddr, size_t slen, size_t alen, char* buf){
  if(alen){
    unsigned idx;
    for(idx = 0 ; idx < alen ; ++idx){
      snprintf(buf + idx * 3, slen - idx * 3, "%02x:",
               ((const unsigned char *)hwaddr)[idx]);
    }
  }else{
    buf[0] = '\0';
  }
  return buf;
}

char* l2addrstr(int l2type, size_t len, const void* addr){
  (void)l2type; // FIXME need for quirks
  size_t slen = hwaddrstrlen(len);
  char* ret = malloc(slen);
  return l2ntop(addr, slen, len, ret);
}

int netstack_print_iface(const netstack_iface* ni, FILE* out){
  int ret = 0;
  const struct rtattr* llrta = netstack_iface_attr(ni, IFLA_ADDRESS);
  char* llstr = NULL;
  if(llrta){
    llstr = l2addrstr(ni->ifi.ifi_type, RTA_PAYLOAD(llrta), RTA_DATA(llrta));
  }
  ret = fprintf(out, "%3d [%*s] %s%cmtu %u\n", ni->ifi.ifi_index,
                (int)sizeof(ni->name) - 1, ni->name, // FIXME
                llstr ? llstr : "", llstr ? ' ' : '\0',
                netstack_iface_mtu(ni));
  free(llstr);
  if(ret < 0){
    return -1;
  }
  return 0;
}

int netstack_print_addr(const netstack_addr* na, FILE* out){
  size_t alen; // 4 for IPv4, 16 for IPv6
  const struct rtattr* nlrta = netstack_addr_attr(na, IFA_ADDRESS);
  if(nlrta == NULL){
    return -1;
  }
  if(na->ifa.ifa_family == AF_INET){
    alen = 4;
  }else if(na->ifa.ifa_family == AF_INET6){
    alen = 16;
  }else{
    return -1;
  }
  if(RTA_PAYLOAD(nlrta) != alen){
    return -1;
  }
  char naddrv[16]; // hold a full raw IPv6 adress
  memcpy(naddrv, RTA_DATA(nlrta), alen);
  char nlstr[INET6_ADDRSTRLEN];
  if(!inet_ntop(na->ifa.ifa_family, naddrv, nlstr, sizeof(nlstr))){
    return -1;
  }
  int ret = 0;
  ret = fprintf(out, "%3d [%s] %s/%u\n", na->ifa.ifa_index,
                family_to_str(na->ifa.ifa_family),
                nlstr, na->ifa.ifa_prefixlen);
  if(ret < 0){
    return -1;
  }
  return 0;
}

int netstack_print_route(const netstack_route* nr, FILE* out){
  int ret = 0;
  ret = fprintf(out, "[%s] %s %s metric %d prio %d in %d out %d\n",
                family_to_str(nr->rt.rtm_family),
                netstack_route_typestr(nr), netstack_route_protstr(nr),
                netstack_route_metric(nr), netstack_route_priority(nr),
                netstack_route_iif(nr), netstack_route_oif(nr));
  if(ret < 0){
    return -1;
  }
  return 0;
}

int netstack_print_neigh(const netstack_neigh* nn, FILE* out){
  int ret = 0;
  ret = fprintf(out, "%3d [%s]\n", nn->nd.ndm_ifindex,
                family_to_str(nn->nd.ndm_family)); // FIXME
  if(ret < 0){
    return -1;
  }
  return 0;
}
