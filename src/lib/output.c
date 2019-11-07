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

int netstack_print_addr(const struct netstack_addr* na, FILE* out){
  const struct rtattr* narta = netstack_addr_attr(na, IFA_ADDRESS);
  if(narta == NULL){
    return -1;
  }
  char nastr[INET6_ADDRSTRLEN];
  int family = netstack_addr_family(na);
  if(!netstack_rtattr_l3addrstr(family, narta, nastr, sizeof(nastr))){
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
    if(!netstack_l3addrstr(family, nrrta, nastr, sizeof(nastr))){
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
  char nastr[INET6_ADDRSTRLEN];
  unsigned family;
  if(!netstack_neigh_l3addrstr(nn, nastr, sizeof(nastr), &family)){
    return -1;
  }
  int ret = 0;
  char* llstr = netstack_neigh_l2addrstr(nn);
  if(llstr == NULL){
    ret = fprintf(out, "%3d [%s] %s\n", netstack_neigh_index(nn),
                  family_to_str(family), nastr);
  }else{
    ret = fprintf(out, "%3d [%s] %s %s\n", netstack_neigh_index(nn),
                  family_to_str(family), nastr, llstr);
    free(llstr);
  }
  if(ret < 0){
    return -1;
  }
  return 0;
}
