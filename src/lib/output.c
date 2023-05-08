#include <stdio.h>
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
  unsigned l2type;
  char* llstr = netstack_iface_addressstr(ni, &l2type);
  char lltype[40];
  char name[IFNAMSIZ];
  netstack_iface_qcounts qc;
  netstack_iface_queuecounts(ni, &qc);
  int irqcount = netstack_iface_irqcount(ni);
  int irq;
  if(irqcount > 0){
    irq = netstack_iface_irq(ni, 0);
  }
  int ret = fprintf(out, "%3d [%s] %s %u %s%smtu %u rxq %d txq %d ",
                    netstack_iface_index(ni),
                    netstack_iface_name(ni, name),
                    netstack_iface_typestr(ni, lltype, sizeof(lltype)), l2type,
                    llstr ? llstr : "", llstr ? " " : "",
                    netstack_iface_mtu(ni),
                    qc.rx, qc.tx);
  free(llstr);
  if(ret < 0){
    return -1;
  }
  int master = netstack_iface_master(ni);
  if(master >= 0){
    ret = fprintf(out, "master %d ", master);
  }
  if(ret < 0){
    return -1;
  }
  if(irqcount > 1){
    ret = fprintf(out, "irqs %d-%d\n", irq, irq + irqcount - 1);
  }else if(irqcount == 1){
    ret = fprintf(out, "irq %d\n", irq);
  }else{
    ret = fprintf(out, "no irqs\n");
  }
  if(ret < 0){
    return -1;
  }
  return 0;
}

int netstack_print_addr(const struct netstack_addr* na, FILE* out){
  unsigned family;
  // an additional byte for a space
  char addrstr[INET6_ADDRSTRLEN];
  if(!netstack_addr_addressstr(na, addrstr, sizeof(addrstr), &family)){
    return -1;
  }
  int ret = 0;
  ret = fprintf(out, "%3d [%s] %s/%u\n", netstack_addr_index(na),
                family_to_str(family), addrstr, netstack_addr_prefixlen(na));
  if(ret < 0){
    return -1;
  }
  return 0;
}

int netstack_print_route(const struct netstack_route* nr, FILE* out){
  unsigned family = 0;
  // an additional byte for a space
  char gwstr[INET6_ADDRSTRLEN + 1] = "";
  if(netstack_route_gatewaystr(nr, gwstr, sizeof(gwstr), &family)){
    strcat(gwstr, " ");
  }
  // an additional 4 for the slash, length, and space
  char dststr[INET6_ADDRSTRLEN + 4] = "";
  char srcstr[INET6_ADDRSTRLEN + 4] = "";
  if(netstack_route_dststr(nr, dststr, sizeof(dststr), &family)){
    snprintf(dststr + strlen(dststr), sizeof(dststr) - strlen(dststr), "/%u ",
             netstack_route_dst_len(nr));
  }
  if(netstack_route_srcstr(nr, srcstr, sizeof(srcstr), &family)){
    snprintf(srcstr + strlen(srcstr), sizeof(srcstr) - strlen(srcstr), "/%u ",
             netstack_route_src_len(nr));
  }
  if(family == 0){
    return -1;
  }
  int ret = 0;
  unsigned rtype = netstack_route_type(nr);
  unsigned proto = netstack_route_protocol(nr);
  ret = fprintf(out, "[%s] %s %s%s%s%s metric %d prio %d in %d out %d\n",
                family_to_str(family), netstack_route_typestr(rtype),
                gwstr, dststr, srcstr,
                netstack_route_protstr(proto),
                netstack_route_metric(nr), netstack_route_priority(nr),
                netstack_route_iif(nr), netstack_route_oif(nr));
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

int netstack_print_stats(const netstack_stats* stats, FILE* out){
  int ret = 0;
  ret = fprintf(out, "%u ifaces %u addrs %u routes %u neighs\n"
                "%ju iface-evs %ju addr-evs %ju route-evs %ju neigh-evs\n"
                "%ju lookup+shares %ju zombies %ju lookup+copies %ju lookup-failures\n"
                "%ju netlink-errors %ju user-callbacks\n",
                stats->ifaces, stats->addrs, stats->routes, stats->neighs,
                stats->iface_events, stats->addr_events,
                stats->route_events, stats->neigh_events,
                stats->lookup_shares, stats->zombie_shares,
                stats->lookup_copies, stats->lookup_failures,
                stats->netlink_errors, stats->user_callbacks_total);
  return ret;
}
