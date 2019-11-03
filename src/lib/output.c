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

static char*
netstack_l2addrstr(int l2type, size_t len, const void* addr){
  (void)l2type; // FIXME need for quirks
  // Each byte becomes two ASCII characters + separator or nul
  size_t slen = ((len) == 0 ? 1 : (len == 1) ? 2 : (len) * 3);
  char* ret = malloc(slen);
  if(ret == NULL){
    return NULL;
  }
  if(len){
    unsigned idx;
    for(idx = 0 ; idx < len ; ++idx){
      snprintf(ret + idx * 3, slen - idx * 3, "%02x:",
               ((const unsigned char *)addr)[idx]);
    }
  }else{
    ret[0] = '\0';
  }
  return ret;
}

int netstack_print_iface(const struct netstack_iface* ni, FILE* out){
  int ret = 0;
  const struct rtattr* llrta = netstack_iface_attr(ni, IFLA_ADDRESS);
  char* llstr = NULL;
  if(llrta){
    llstr = netstack_l2addrstr(netstack_iface_family(ni), RTA_PAYLOAD(llrta),
                               RTA_DATA(llrta));
  }
  char name[IFNAMSIZ];
  ret = fprintf(out, "%3d [%s] %u %s%smtu %u\n", netstack_iface_index(ni),
                netstack_iface_name(ni, name), netstack_iface_type(ni),
                llstr ? llstr : "", llstr ? " " : "",
                netstack_iface_mtu(ni));
  free(llstr);
  if(ret < 0){
    return -1;
  }
  return 0;
}

static char*
l3addrstr(int l3fam, const struct rtattr* rta, char* str, size_t slen){
  size_t alen; // 4 for IPv4, 16 for IPv6
  if(l3fam == AF_INET){
    alen = 4;
  }else if(l3fam == AF_INET6){
    alen = 16;
  }else{
    return NULL;
  }
  if(RTA_PAYLOAD(rta) != alen){
    return NULL;
  }
  char naddrv[16]; // hold a full raw IPv6 adress
  memcpy(naddrv, RTA_DATA(rta), alen);
  if(!inet_ntop(l3fam, naddrv, str, slen)){
    return NULL;
  }
  return str;
}

int netstack_print_addr(const struct netstack_addr* na, FILE* out){
  const struct rtattr* narta = netstack_addr_attr(na, IFA_ADDRESS);
  if(narta == NULL){
    return -1;
  }
  char nastr[INET6_ADDRSTRLEN];
  int family = netstack_addr_family(na);
  if(!l3addrstr(family, narta, nastr, sizeof(nastr))){
    return -1;
  }
  int ret = 0;
  ret = fprintf(out, "%3d [%s] %s/%u\n", netstack_addr_index(na),
                family_to_str(family), nastr, netstack_addr_prefixlen(na));
  if(ret < 0){
    return -1;
  }
  return 0;
}

int netstack_print_route(const struct netstack_route* nr, FILE* out){
  const struct rtattr* nrrta = netstack_route_attr(nr, RTA_DST);
  int ret = 0;
  unsigned rtype = netstack_route_type(nr);
  unsigned proto = netstack_route_proto(nr);
  unsigned family = netstack_route_family(nr);
  if(nrrta == NULL){
    ret = fprintf(out, "[%s] default %s %s metric %d prio %d in %d out %d\n",
                  family_to_str(family),
                  netstack_route_typestr(rtype), netstack_route_protstr(proto),
                  netstack_route_metric(nr), netstack_route_priority(nr),
                  netstack_route_iif(nr), netstack_route_oif(nr));
  }else{
    char nastr[INET6_ADDRSTRLEN];
    if(!l3addrstr(family, nrrta, nastr, sizeof(nastr))){
      return -1;
    }
    ret = fprintf(out, "[%s] %s/%u %s %s metric %d prio %d in %d out %d\n",
                  family_to_str(family), nastr, netstack_route_dst_len(nr),
                  netstack_route_typestr(rtype), netstack_route_protstr(proto),
                  netstack_route_metric(nr), netstack_route_priority(nr),
                  netstack_route_iif(nr), netstack_route_oif(nr));
  }
  if(ret < 0){
    return -1;
  }
  return 0;
}

int netstack_print_neigh(const struct netstack_neigh* nn, FILE* out){
  const struct rtattr* nnrta = netstack_neigh_attr(nn, NDA_DST);
  if(nnrta == NULL){
    return -1;
  }
  unsigned family = netstack_neigh_family(nn);
  char nastr[INET6_ADDRSTRLEN];
  if(!l3addrstr(family, nnrta, nastr, sizeof(nastr))){
    return -1;
  }
  int ret = 0;
  const struct rtattr* l2rta = netstack_neigh_attr(nn, NDA_LLADDR);
  if(l2rta){
    char* llstr = netstack_l2addrstr(0, RTA_PAYLOAD(l2rta), RTA_DATA(l2rta));
    if(!llstr){
      return -1;
    }
    ret = fprintf(out, "%3d [%s] %s %s\n", netstack_neigh_index(nn),
                  family_to_str(family), nastr, llstr);
    free(llstr);
  }else{
    ret = fprintf(out, "%3d [%s] %s\n", netstack_neigh_index(nn),
                  family_to_str(family), nastr);
  }
  if(ret < 0){
    return -1;
  }
  return 0;
}
