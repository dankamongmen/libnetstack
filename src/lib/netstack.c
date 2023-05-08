#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netlink/msg.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <netlink/socket.h>
#include <netlink/netlink.h>
#include <linux/rtnetlink.h>
#include "netstack.h"

// convert an RTA into a uint64_t
static inline int
rtattrtou32(const struct rtattr* rta, uint32_t* ul){
  if(rta == NULL){
    return -1;
  }
  *ul = *(const uint32_t*)RTA_DATA(rta); // ugh, yes, this is how it's done
  return 0;
}

// Naive hash. Interface numbers are assigned successively, so there ought
// generally not be clashes with a linear mod map.
#define IFACE_HASH_SLOTS 256

// each of these types corresponds to a different rtnetlink message type. we
// copy the payload directly from the netlink message to rtabuf, but that form
// requires o(n) to get to any given attribute. we store a table of n offsets
// into this buffer at rta_index. if we're running on a newer kernel, we might
// get an attribute larger than we're prepared to handle. that's fine.
// interested parties can still extract it using rtnetlink(3) macros. the
// convenience functions netstack_*_attr() are provided for this purpose: each
// will retrieve the value via lookup if less than the MAX against which we
// were compiled, and do an o(n) check otherwise.
typedef struct netstack_iface {
  struct ifinfomsg ifi;
  char name[IFNAMSIZ]; // NUL-terminated, safely processed from IFLA_NAME
  struct rtattr* rtabuf; // copied directly from message
  size_t rtabuflen; // number of bytes copied to rtabuf
  // set up before invoking the user callback, these allow for o(1) index into
  // rtabuf by attr type. NULL if that attr wasn't in the message. We use
  // offsets rather than pointers lest deep copy require recomputing the index.
  // They are 1-biased so that 0 works as a sentinel, indicating no attr.
  size_t rta_index[__IFLA_MAX];
  bool unknown_attrs; // are there attrs >= __IFLA_MAX?
  struct netstack_iface* hnext; // next in the idx-hashed table ns->iface_slots
  atomic_int refcount; // netstack and/or client(s) can share objects
} netstack_iface;

typedef struct netstack_addr {
  struct ifaddrmsg ifa;
  struct rtattr* rtabuf;        // copied directly from message
  size_t rtabuflen;
  size_t rta_index[__IFA_MAX];
  bool unknown_attrs;  // are there attrs >= __IFA_MAX?
} netstack_addr;

typedef struct netstack_neigh {
  struct ndmsg nd;
  struct rtattr* rtabuf;        // copied directly from message
  size_t rtabuflen;
  size_t rta_index[__NDA_MAX];
  bool unknown_attrs;  // are there attrs >= __NDA_MAX?
} netstack_neigh;

typedef struct netstack_route {
  struct rtmsg rt;
  struct rtattr* rtabuf;        // copied directly from message
  size_t rtabuflen;
  size_t rta_index[__RTA_MAX];
  bool unknown_attrs;  // are there attrs >= __RTA_MAX?
} netstack_route;

// trie on names
typedef struct name_node {
  netstack_iface *iface;        // iface at this node, can be NULL
  struct name_node* array[256]; // array of pointers to name_nodes
} name_node;

typedef struct netstack {
  struct nl_sock* nl;  // netlink connection abstraction from libnl
  pthread_t rxtid;
  pthread_t txtid;
  // We can only have one command of the class e.g. DUMP outstanding at a time.
  // Queue up any others for transmission when possible.
  // Iff txqueue[dequeueidx] == -1, there are no messages to send.
  // Iff txqueue[queueidx] >= 0, there is no room to enqueue messages.
  int txqueue[128];
  int dequeueidx, queueidx;
  // Unset by the txthread, set by the rxthread (and initializer).
  pthread_cond_t txcond;
  pthread_mutex_t txlock;
  atomic_bool clear_to_send;
  // Statistics
  atomic_uintmax_t netlink_errors;
  atomic_uintmax_t user_callbacks_total;
  atomic_uintmax_t lookup_copies, lookup_shares, lookup_failures;
  atomic_uintmax_t iface_events, addr_events, route_events, neigh_events;
  // Guards iface_hash and the hnext pointer of all netstack_ifaces. Does not
  // guard netstack_ifaces' reference counts *aside from* the case when we've
  // just looked the object up, and are about to share it. We must make that
  // change while holding the lock, to ensure it is not removed from underneath
  // us. Clients needn't take this lock when downing the reference count, since
  // if it hits 0 under their watch, it cannot be in the netstack hash any
  // longer (or it would still have a reference).
  pthread_mutex_t hashlock;
  netstack_iface* iface_hash[IFACE_HASH_SLOTS];
  unsigned iface_count; // ifaces currently in the active cache
  uint64_t iface_bytes; // bytes occupied (not including metadata) in cache
  uint64_t nonce; // incremented with every change to invalidate streamings
  name_node* name_trie; // all netstack_iface objects, indexed by name
  netstack_opts opts; // copied wholesale in netstack_create()
} netstack;

// add a request to the txqueue, if there's room
static int
queue_request(netstack* ns, int req){
  bool queued = false;
  pthread_mutex_lock(&ns->txlock);
  if(ns->txqueue[ns->queueidx] == -1){
    ns->txqueue[ns->queueidx] = req;
    if(++ns->queueidx == sizeof(ns->txqueue) / sizeof(*ns->txqueue)){
      ns->queueidx = 0;
    }
    if(ns->queueidx != ns->dequeueidx){
      ns->txqueue[ns->queueidx] = -1;
    }
    queued = true;
  }
  pthread_mutex_unlock(&ns->txlock);
  pthread_cond_signal(&ns->txcond);
  return queued ? 0 : -1;
}

static void
destroy_name_trie(name_node* node){
  if(node){
    size_t z;
    for(z = 0 ; z < sizeof(node->array) / sizeof(*node->array) ; ++z){
      destroy_name_trie(node->array[z]);
    }
    free(node);
  }
}

static name_node*
create_name_node(netstack_iface* iface){
  name_node* n = malloc(sizeof(*n));
  if(n){
    memset(n, 0, sizeof(*n));
    n->iface = iface;
  }
  return n;
}

// Returns any netstack_iface we happen to replace. ni == NULL to purge.
static netstack_iface*
name_trie_exchange(name_node** node, netstack_iface* ni, const char* name){
  while(*name){
    if(*node == NULL){
      if((*node = create_name_node(NULL)) == NULL){
        return NULL; // FIXME remove added nodes?
      }
    }
    node = &((*node)->array[*(const unsigned char*)(name++)]);
  }
  netstack_iface* replaced;
  if(*node){
    replaced = (*node)->iface;
    (*node)->iface = ni;
  }else{
    replaced = NULL;
    *node = ni ? create_name_node(ni) : NULL;
  }
  return replaced;
}

// Returns the replaced node, if any
static inline netstack_iface*
name_trie_add(name_node** node, netstack_iface* ni){
  return name_trie_exchange(node, ni, ni->name);
}

// Returns the purged node, if any
static inline netstack_iface*
name_trie_purge(name_node** node, const char* name){
  return name_trie_exchange(node, NULL, name);
}

static inline int
iface_hash(const netstack* ns, int index){
  return index % (sizeof(ns->iface_hash) / sizeof(*ns->iface_hash));
}

// Sits on blocking nl_recvmsgs()
static void*
netstack_rx_thread(void* vns){
  netstack* ns = vns;
  int ret;
  while((ret = nl_recvmsgs_default(ns->nl)) == 0){
    // FIXME ensure it matched what we expect?
    ns->clear_to_send = true;
    pthread_cond_broadcast(&ns->txcond);
  }
  ns->opts.diagfxn("Error rxing from netlink socket (%s)\n", nl_geterror(ret));
  // FIXME recover?
  return NULL;
}

static void
tx_cancel_clean(void* vns){
  netstack* ns = vns;
  pthread_mutex_unlock(&ns->txlock);
}

// Sits on condition variable, transmitting when there's data in the txqueue
static void*
netstack_tx_thread(void* vns){
  netstack* ns = vns;
  while(true){
    pthread_mutex_lock(&ns->txlock);
    pthread_cleanup_push(tx_cancel_clean, ns);
    while(!ns->clear_to_send || ns->txqueue[ns->dequeueidx] == -1){
      pthread_cond_wait(&ns->txcond, &ns->txlock);
    }
    ns->clear_to_send = false;
    struct rtgenmsg rt = {
      .rtgen_family = AF_UNSPEC,
    };
    if(nl_send_simple(ns->nl, ns->txqueue[ns->dequeueidx],
                      NLM_F_REQUEST|NLM_F_DUMP, &rt, sizeof(rt)) < 0){
      // FIXME do what?
    }
    ns->txqueue[ns->dequeueidx++] = -1;
    pthread_cleanup_pop(1);
  }
  return NULL;
}

static bool
iface_rta_handler(netstack_iface* ni, const struct ifinfomsg* ifi,
                  size_t rtaoff, int* rlen __attribute__ ((unused))){
  const struct rtattr* rta = (const struct rtattr*)
    (((const char*)(ni->rtabuf)) + rtaoff);
  memcpy(&ni->ifi, ifi, sizeof(*ifi));
  if(rta->rta_type > IFLA_MAX){
    // FIXME need ns ns->opts.diagfxn("Unknown IFLA_RTA type %d len %d\n", rta->rta_type, *rlen);
    ni->unknown_attrs = true;
    return true;
  }
  if(rta->rta_type == IFLA_IFNAME){
    size_t max = sizeof(ni->name) > RTA_PAYLOAD(rta) ?
                  RTA_PAYLOAD(rta) : sizeof(ni->name);
    size_t nlen = strnlen(RTA_DATA(rta), max);
    if(nlen == max){
      // FIXME need ns ns->opts.diagfxn("Invalid name [%.*s]\n", (int)max, (const char*)(RTA_DATA(rta)));
      return false;
    }
    memcpy(ni->name, RTA_DATA(rta), nlen + 1);
  }
  ni->rta_index[rta->rta_type] = rtaoff + 1;
  return true;
}

static bool
addr_rta_handler(netstack_addr* na, const struct ifaddrmsg* ifa,
                  size_t rtaoff, int* rlen __attribute__ ((unused))){
  const struct rtattr* rta = (const struct rtattr*)
    (((const char*)(na->rtabuf)) + rtaoff);
  memcpy(&na->ifa, ifa, sizeof(*ifa));
  if(rta->rta_type > IFA_MAX){
    // FIXME need ns ns->opts.diagfxn("Unknown IFA_RTA type %d len %d\n", rta->rta_type, *rlen);
    na->unknown_attrs = true;
    return true;
  }
  na->rta_index[rta->rta_type] = rtaoff + 1;
  return true;
}

static bool
route_rta_handler(netstack_route* nr, const struct rtmsg* rt,
                  size_t rtaoff, int* rlen __attribute__ ((unused))){
  const struct rtattr* rta = (const struct rtattr*)
    (((const char*)(nr->rtabuf)) + rtaoff);
  memcpy(&nr->rt, rt, sizeof(*rt));
  if(rta->rta_type > RTA_MAX){
      // FIXME need ns ns->opts.diagfxn("Unknown RTN_RTA type %d len %d\n", rta->rta_type, *rlen);
      nr->unknown_attrs = true;
      return true;
  }
  nr->rta_index[rta->rta_type] = rtaoff + 1;
  return true;
}

static bool
neigh_rta_handler(netstack_neigh* nn, const struct ndmsg* nd,
                  size_t rtaoff, int* rlen __attribute__ ((unused))){
  const struct rtattr* rta = (const struct rtattr*)
    (((const char*)(nn->rtabuf)) + rtaoff);
  memcpy(&nn->nd, nd, sizeof(*nd));
  if(rta->rta_type > NDA_MAX){
    // FIXME need ns ns->opts.diagfxn("Unknown ND_RTA type %d len %d\n", rta->rta_type, *rlen);
    nn->unknown_attrs = true;
    return true;
  }
  nn->rta_index[rta->rta_type] = rtaoff + 1;
  return true;
}

// FIXME xmacro all of these out
static bool
viface_rta_handler(void* v1, const void* v2, size_t rtaoff, int* rlen){
  return iface_rta_handler(v1, v2, rtaoff, rlen);
}

static bool
vaddr_rta_handler(void* v1, const void* v2, size_t rtaoff, int* rlen){
  return addr_rta_handler(v1, v2, rtaoff, rlen);
}

static bool
vroute_rta_handler(void* v1, const void* v2, size_t rtaoff, int* rlen){
  return route_rta_handler(v1, v2, rtaoff, rlen);
}

static bool
vneigh_rta_handler(void* v1, const void* v2, size_t rtaoff, int* rlen){
  return neigh_rta_handler(v1, v2, rtaoff, rlen);
}

static inline void*
memdup(const void* v, size_t n){
  void* ret = malloc(n);
  if(ret){
    memcpy(ret, v, n);
  }
  return ret;
}

static inline struct rtattr*
rtas_dup(const struct rtattr* rtas, int rlen,
         size_t* rta_index, size_t rtamax){
  struct rtattr* ret = memdup(rtas, rlen);
  // we needn't recompute this table, since it's all relative offsets
  if(ret){
    memset(rta_index, 0, sizeof(*rta_index) * rtamax);
  }
  return ret;
}

static netstack_iface*
create_iface(const struct rtattr* rtas, int rlen){
  netstack_iface* ni;
  ni = malloc(sizeof(*ni));
  memset(ni, 0, sizeof(*ni));
  atomic_init(&ni->refcount, 1);
  ni->rtabuf = rtas_dup(rtas, rlen, ni->rta_index,
                        sizeof(ni->rta_index) / sizeof(*ni->rta_index));
  return ni;
}

static inline void*
vcreate_iface(const struct rtattr* rtas, int rlen){
  return create_iface(rtas, rlen);
}

static netstack_addr*
create_addr(const struct rtattr* rtas, int rlen){
  netstack_addr* na;
  na = malloc(sizeof(*na));
  memset(na, 0, sizeof(*na));
  na->rtabuf = rtas_dup(rtas, rlen, na->rta_index,
                        sizeof(na->rta_index) / sizeof(*na->rta_index));
  return na;
}

static inline void*
vcreate_addr(const struct rtattr* rtas, int rlen){
  return create_addr(rtas, rlen);
}

static netstack_route*
create_route(const struct rtattr* rtas, int rlen){
  netstack_route* nr;
  nr = malloc(sizeof(*nr));
  memset(nr, 0, sizeof(*nr));
  nr->rtabuf = rtas_dup(rtas, rlen, nr->rta_index,
                        sizeof(nr->rta_index) / sizeof(*nr->rta_index));
  return nr;
}

static inline void*
vcreate_route(const struct rtattr* rtas, int rlen){
  return create_route(rtas, rlen);
}

static netstack_neigh*
create_neigh(const struct rtattr* rtas, int rlen){
  netstack_neigh* nn;
  nn = malloc(sizeof(*nn));
  memset(nn, 0, sizeof(*nn));
  nn->rtabuf = rtas_dup(rtas, rlen, nn->rta_index,
                        sizeof(nn->rta_index) / sizeof(*nn->rta_index));
  return nn;
}

static inline void*
vcreate_neigh(const struct rtattr* rtas, int rlen){ return create_neigh(rtas, rlen); }

static void
netstack_iface_destroy(netstack_iface* ni){
  if(ni){
    int refs = atomic_fetch_sub(&ni->refcount, 1);
    if(refs == 1){
      free(ni->rtabuf);
      free(ni);
    }
  }
}

static void free_addr(netstack_addr* na){
  if(na){
    free(na->rtabuf);
    free(na);
  }
}

static void free_route(netstack_route* nr){
  if(nr){
    free(nr->rtabuf);
    free(nr);
  }
}

static void free_neigh(netstack_neigh* nn){
  if(nn){
    free(nn->rtabuf);
    free(nn);
  }
}

static inline void vfree_iface(void* vni){ netstack_iface_destroy(vni); }
static inline void vfree_addr(void* va){ free_addr(va); }
static inline void vfree_route(void* vr){ free_route(vr); }
static inline void vfree_neigh(void* vn){ free_neigh(vn); }

#ifndef NDA_RTA
#define NDA_RTA(r) \
 ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ndmsg))))
#endif
#ifndef NDA_PAYLOAD
#define NDA_PAYLOAD(n) \
 NLMSG_PAYLOAD((n), sizeof(struct ndmsg))
#endif

static bool
validate_enumeration_flags(const uint32_t* offsets, int n, void* objs,
                           size_t obytes, netstack_enumerator* streamer){
  if(n < 0){ // in no case may n be negative
    return false;
  }
  if((n && !offsets) || (!n && offsets)){
    return false;
  }
  if((obytes && !objs) || (!obytes && objs)){
    return false;
  }
  if(streamer && streamer->slot >= IFACE_HASH_SLOTS){
    return false;
  }
  return true;
}

// Size, in bytes, necessary to represent this ni (varies from ni to ni)
static inline size_t
netstack_iface_size(const netstack_iface* ni){
  return sizeof(ni) + ni->rtabuflen;
}

unsigned netstack_iface_count(const netstack* ns){
  netstack* unsafe_ns = (netstack*)ns;
  unsigned ret;
  pthread_mutex_lock(&unsafe_ns->hashlock);
  ret = ns->iface_count;
  pthread_mutex_unlock(&unsafe_ns->hashlock);
  return ret;
}

int netstack_iface_stats_refresh(netstack* ns){
  if(queue_request(ns, RTM_GETLINK)){
    return -1;
  }
  // FIXME need to wait on response!
  return 0;
}

static int
netstack_iface_irqinfo(const netstack_iface* ni, unsigned long* minirq, unsigned long* maxirq){
  int sfd = open("/sys/class/net", O_CLOEXEC | O_DIRECTORY | O_RDONLY);
  if(sfd < 0){
    return -1;
  }
  int dfd = openat(sfd, ni->name, O_CLOEXEC | O_DIRECTORY | O_RDONLY);
  close(sfd);
  if(dfd < 0){
    return -1;
  }
  int mfd = openat(dfd, "device/msi_irqs", O_CLOEXEC | O_DIRECTORY | O_RDONLY);
  close(dfd);
  if(mfd < 0){
    return -1;
  }
  DIR* d = fdopendir(mfd);
  // the argument passed to a successfuly fdopendir() must not be further used
  if(!d){
    close(mfd); // but we must close it here
    return -1;
  }
  *minirq = ULONG_MAX;
  *maxirq = 0;
  struct dirent* dent;
  while( (dent = readdir(d)) ){
    if(dent->d_type == DT_DIR){
      continue;
    }
    char* endp;
    errno = 0;
    unsigned long irqname = strtoul(dent->d_name, &endp, 0);
    if(*endp || (irqname == ULONG_MAX && errno == ERANGE)){
      continue;
    }
    if(irqname < *minirq){
      *minirq = irqname;
    }
    if(irqname > *maxirq){
      *maxirq = irqname;
    }
  }
  closedir(d);
  if(*minirq > *maxirq){
    return -1;
  }
  return 0;
}

int netstack_iface_irq(const netstack_iface* ni, unsigned qidx){
  unsigned long min, max;
  if(netstack_iface_irqinfo(ni, &min, &max) < 0){
    return -1;
  }
  // FIXME this assumes IRQs are contiguous, and i have no idea whether
  // that's true. it is true for all devices i've checked.
  if(ULONG_MAX - qidx < min){
    return -1;
  }
  if(min + qidx > max){
    return -1;
  }
  return min + qidx;
}

unsigned netstack_iface_irqcount(const struct netstack_iface* ni){
  unsigned long min, max;
  if(netstack_iface_irqinfo(ni, &min, &max) < 0){
    return -1;
  }
  return max - min + 1;
}

int netstack_iface_enumerate(const netstack* ns, uint32_t* offsets, int* n,
                             void* objs, size_t* obytes,
                             netstack_enumerator* streamer){
  if(!validate_enumeration_flags(offsets, *n, objs, *obytes, streamer)){
    return -1;
  }
  int copied = 0;
  uint64_t copied_bytes = 0;
  netstack* unsafe_ns = (netstack*)ns;
  unsigned sslot = streamer ? streamer->slot : 0; // FIXME hnext
  pthread_mutex_lock(&unsafe_ns->hashlock);
  if(streamer && streamer->nonce && streamer->nonce != ns->nonce){
    pthread_mutex_unlock(&unsafe_ns->hashlock);
    return -1;
  }
  const size_t tsize = ns->iface_bytes;
  if(tsize > *obytes && !streamer){ // no streamer means atomic request
    *obytes = tsize;
    *n = ns->iface_count;
    pthread_mutex_unlock(&unsafe_ns->hashlock);
    return -1;
  }
  unsigned z;
  const netstack_iface* ni;
  for(z = sslot ; z < sizeof(ns->iface_hash) / sizeof(*ns->iface_hash) ; ++z){
    ni = ns->iface_hash[z];
    while(ni){
      if(copied == *n){
        goto exhausted;
      }
      const size_t nisize = netstack_iface_size(ni);
      if(*obytes - copied_bytes < nisize){
        goto exhausted;
      }
      offsets[copied] = copied_bytes; // where the new netstack_iface starts
      netstack_iface* targni = (netstack_iface*)((char*)objs + copied_bytes);
      memcpy(targni, ni, sizeof(*ni));
      copied_bytes += sizeof(*ni);
      targni->hnext = NULL;
      // These don't need to be freed up -- all the resources have been
      // provided by the caller. We only free when refs == 1, so init to 0.
      atomic_init(&targni->refcount, 0);
      targni->rtabuf = (struct rtattr*)((char*)objs + copied_bytes);
      memcpy(targni->rtabuf, ni->rtabuf, ni->rtabuflen);
      copied_bytes += ni->rtabuflen;
      // copied_bytes ought have increased by netstack_iface_size() in total
      ni = ni->hnext;
      ++copied;
    }
  }
exhausted:
  // if we're not yet done, just out of memory, we need to set up streaming.
  // either way, set our new values.
  if(z < sizeof(ns->iface_hash) / sizeof(*ns->iface_hash) || ni){
    *n -= copied;
    *obytes -= copied_bytes;
    streamer->nonce = ns->nonce;
    streamer->slot = z;
    if(ni){
      streamer->hnext = (uintptr_t)ni->hnext;
    }else{
      streamer->hnext = 0;
    }
  }else{
    *n = 0;
    *obytes = 0;
    streamer->nonce = 0;
    streamer->slot = 0;
    streamer->hnext = 0;
  }
  pthread_mutex_unlock(&unsafe_ns->hashlock);
  return copied;
}

static inline void
viface_cb(netstack* ns, netstack_event_e etype, void* vni){
  netstack_iface* ni = vni;
  // We might be replacing some previous element. If so, that one comes out of
  // the hash as replaced, and should have its refcount dropped.
  netstack_iface* replaced = NULL;
  const size_t nisize = netstack_iface_size(ni);
  int hidx = iface_hash(ns, ni->ifi.ifi_index);
  // If we're not tracking interfaces, we don't need to manipulate the cache at
  // all, so skip all of this. We furthermore free the object before return.
  if(!ns->opts.iface_notrack){
    pthread_mutex_lock(&ns->hashlock);
    netstack_iface** tmp = &ns->iface_hash[hidx];
    if(etype != NETSTACK_DEL){ // insert into caches
      name_trie_add(&ns->name_trie, ni);
      ni->hnext = *tmp; // we always insert into the front of hlist
      *tmp = ni;
      tmp = &ni->hnext;
      ++ns->iface_count;
      ns->iface_bytes += nisize;
    }else{ // purge from caches
      netstack_iface* purged = name_trie_purge(&ns->name_trie, ni->name);
      if(purged){
        --ns->iface_count;
        ns->iface_bytes -= netstack_iface_size(purged);
      }
    }
    while(*tmp){ // need to see if one ought be removed (matches our key)
      if((*tmp)->ifi.ifi_index == ni->ifi.ifi_index){
        replaced = *tmp;
        *tmp = (*tmp)->hnext;
        break;
      }
      tmp = &(*tmp)->hnext;
    }
    // We might have changed names (but retained our index). In this case, we've
    // already added the new name (and iface) to the name_trie. In the case where
    // we retained the name and idx, we've replaced the object in the name_trie.
    // For the former case, we need remove the old name.
    if(replaced && strcmp(ni->name, replaced->name)){
      name_trie_purge(&ns->name_trie, replaced->name);
      --ns->iface_count;
      ns->iface_bytes -= netstack_iface_size(replaced);
    }
    pthread_mutex_unlock(&ns->hashlock);
    if(replaced){
      netstack_iface_destroy(replaced);
    }
  }
  if(ns->opts.iface_cb){
    ns->opts.iface_cb(ni, etype, ns->opts.iface_curry);
    atomic_fetch_add(&ns->user_callbacks_total, 1);
  }
  if(etype == NETSTACK_DEL || ns->opts.iface_notrack){
    netstack_iface_destroy(ni);
  }
  atomic_fetch_add(&ns->iface_events, 1);
}

static inline void
vaddr_cb(netstack* ns, netstack_event_e etype, void* vna){
  if(ns->opts.addr_cb){
    ns->opts.addr_cb(vna, etype, ns->opts.addr_curry);
    atomic_fetch_add(&ns->user_callbacks_total, 1);
  }
  atomic_fetch_add(&ns->addr_events, 1);
  free_addr(vna);
}

static inline void
vroute_cb(netstack* ns, netstack_event_e etype, void* vnr){
  if(ns->opts.route_cb){
    ns->opts.route_cb(vnr, etype, ns->opts.route_curry);
    atomic_fetch_add(&ns->user_callbacks_total, 1);
  }
  atomic_fetch_add(&ns->route_events, 1);
  free_route(vnr);
}

static inline void
vneigh_cb(netstack* ns, netstack_event_e etype, void* vnn){
  if(ns->opts.neigh_cb){
    ns->opts.neigh_cb(vnn, etype, ns->opts.neigh_curry);
    atomic_fetch_add(&ns->user_callbacks_total, 1);
  }
  atomic_fetch_add(&ns->neigh_events, 1);
  free_neigh(vnn);
}

static int
msg_handler_internal(struct nl_msg* msg, netstack* ns){
  struct nlmsghdr* nhdr = nlmsg_hdr(msg);
  int nlen = nhdr->nlmsg_len;
  while(nlmsg_ok(nhdr, nlen)){
    const int ntype = nhdr->nlmsg_type;
    const struct rtattr *rta = NULL;
    const struct ifinfomsg* ifi = NLMSG_DATA(nhdr);
    const struct ifaddrmsg* ifa = NLMSG_DATA(nhdr);
    const struct rtmsg* rt = NLMSG_DATA(nhdr);
    const struct ndmsg* nd = NLMSG_DATA(nhdr);
    const void* hdr = NULL; // aliases one of the NLMSG_DATA lvalues above
    size_t hdrsize = 0; // size of leading object (hdr), depends on message type
    // processor for rtattr objects in this type regime. takes the newly-created
    // netstack_* object (newobj), the leading type-dependent object (aliased
    // by hdr), the offset of the RTA being handled, and &rlen.
    bool (*pfxn)(void*, const void*, size_t, int*);
    void (*dfxn)(void*); // destroyer of this type of object
    void (*cfxn)(netstack*, netstack_event_e, void*); // user callback wrapper
    void* (*gfxn)(const struct rtattr*, int); // constructor
    netstack_event_e etype;
    switch(ntype){
      case RTM_DELLINK: // intentional fallthrough
      case RTM_NEWLINK:
        hdr = ifi;
        rta = IFLA_RTA(ifi);
        hdrsize = sizeof(*ifi);
        pfxn = viface_rta_handler;
        dfxn = vfree_iface;
        cfxn = viface_cb;
        gfxn = vcreate_iface;
        etype = (ntype == RTM_DELLINK) ? NETSTACK_DEL : NETSTACK_MOD;
        break;
      case RTM_DELADDR: // intentional fallthrough
      case RTM_NEWADDR:
        hdr = ifa;
        rta = IFA_RTA(ifa);
        hdrsize = sizeof(*ifa);
        pfxn = vaddr_rta_handler;
        dfxn = vfree_addr;
        cfxn = vaddr_cb;
        gfxn = vcreate_addr;
        etype = (ntype == RTM_DELADDR) ? NETSTACK_DEL : NETSTACK_MOD;
        break;
      case RTM_DELROUTE: // intentional fallthrough
      case RTM_NEWROUTE:
        hdr = rt;
        rta = RTM_RTA(rt);
        hdrsize = sizeof(*rt);
        pfxn = vroute_rta_handler;
        dfxn = vfree_route;
        cfxn = vroute_cb;
        gfxn = vcreate_route;
        etype = (ntype == RTM_DELROUTE) ? NETSTACK_DEL : NETSTACK_MOD;
        break;
      case RTM_DELNEIGH: // intentional fallthrough
      case RTM_NEWNEIGH:
        hdr = nd;
        rta = NDA_RTA(nd);
        hdrsize = sizeof(*nd);
        pfxn = vneigh_rta_handler;
        dfxn = vfree_neigh;
        cfxn = vneigh_cb;
        gfxn = vcreate_neigh;
        etype = (ntype == RTM_DELNEIGH) ? NETSTACK_DEL : NETSTACK_MOD;
        break;
      default: ns->opts.diagfxn("Unknown nl type: %d\n", ntype); break;
    }
    if(hdrsize == 0){
      break;
    }
    const struct rtattr* riter = rta;
    // FIXME factor all of this out probably
    int rlen = nlen - NLMSG_LENGTH(hdrsize);
    void* newobj = gfxn(rta, rlen);
    // always there is an RTA extraction pfxn
    while(RTA_OK(riter, rlen)){
      if(!pfxn(newobj, hdr, (char*)riter - (char*)rta, &rlen)){
        break;
      }
      riter = RTA_NEXT(riter, rlen);
    }
    if(rlen){
      dfxn(newobj);
      ns->opts.diagfxn("Netlink attr was invalid, %db left\n", rlen);
      return NL_SKIP;
    }
    cfxn(ns, etype, newobj);
    nhdr = nlmsg_next(nhdr, &nlen);
  }
  if(nlen){
    ns->opts.diagfxn("Netlink message was invalid, %db left\n", nlen);
    return NL_SKIP;
  }
  return NL_OK;
}

static int
msg_handler(struct nl_msg* msg, void* vns){
  int oldcancelstate, ret;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldcancelstate);
  ret = msg_handler_internal(msg, vns);
  pthread_setcancelstate(oldcancelstate, &oldcancelstate);
  return ret;
}

static int
err_handler(struct sockaddr_nl* nla, struct nlmsgerr* nlerr, void* vns){
  netstack* ns = vns;
  ns->opts.diagfxn("Netlink error (fam %d) %d (%s)\n", nla->nl_family,
          -nlerr->error, strerror(-nlerr->error));
  atomic_fetch_add(&ns->netlink_errors, 1);
  return NL_OK;
}

void netstack_stderr_diag(const char* fmt, ...){
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

static void
null_diagfxn(const char* fmt, ...){
  (void)fmt;
}

static bool
validate_options(const netstack_opts* nopts){
  // NULL? No problem! All zeroes maps to all defaults, is all good!
  if(nopts == NULL){
    return true;
  }
  // Without a callback, do not allow a meaningless curry to be specified
  if(nopts->iface_curry && !nopts->iface_cb){
    return false;
  }
  if(nopts->addr_curry && !nopts->addr_cb){
    return false;
  }
  if(nopts->route_curry && !nopts->route_cb){
    return false;
  }
  if(nopts->neigh_curry && !nopts->neigh_cb){
    return false;
  }
  // Must have at least some kind of action configured (callback or track)
  if(!nopts->addr_cb && !nopts->neigh_cb && !nopts->route_cb && !nopts->iface_cb){
    if(nopts->addr_notrack && nopts->neigh_notrack && nopts->route_notrack && nopts->iface_notrack){
      return false;
    }
  }
  return true;
}

// Filter the specified netlink dumper message from dumpmsgs, if it happens to
// be there. *dumpcount will be updated in that case.
static void
filter_netlink_dumper(int* dumpmsgs, int* dumpcount, int dumper){
  int z;
  for(z = 0 ; z < *dumpcount ; ++z){
    if(dumpmsgs[z] == dumper){
      dumpmsgs[z] = dumpmsgs[--*dumpcount];
      break;
    }
  }
}

// Determine which groups we want to subscribe to based off the
// netstack_options, and subscribe to them. dumpmsgs will be filtered based
// off what we subscribe to; it ought contain at first all possible dumpers,
// of which there are dumpcount. the number remaining will be stored there.
static int
subscribe_to_netlink(const netstack* ns, int* dumpmsgs, int* dumpcount){
  // We don't use nl_socket_add_memberships due to its weird varargs API.
  if(ns->opts.iface_cb || !ns->opts.iface_notrack){
    if(nl_socket_add_memberships(ns->nl, RTNLGRP_LINK, NFNLGRP_NONE)){
      return -1;
    }
  }else{
    filter_netlink_dumper(dumpmsgs, dumpcount, RTM_GETLINK);
  }
  if(ns->opts.addr_cb || !ns->opts.addr_notrack){
    if(nl_socket_add_memberships(ns->nl, RTNLGRP_IPV4_IFADDR,
                                 RTNLGRP_IPV6_IFADDR, NFNLGRP_NONE)){
      return -1;
    }
  }else{
    filter_netlink_dumper(dumpmsgs, dumpcount, RTM_GETADDR);
  }
  if(ns->opts.route_cb || !ns->opts.route_notrack){
    if(nl_socket_add_memberships(ns->nl, RTNLGRP_IPV4_ROUTE,
                                 RTNLGRP_IPV6_ROUTE, NFNLGRP_NONE)){
      return -1;
    }
  }else{
    filter_netlink_dumper(dumpmsgs, dumpcount, RTM_GETROUTE);
  }
  if(ns->opts.neigh_cb || !ns->opts.neigh_notrack){
    if(nl_socket_add_memberships(ns->nl, RTNLGRP_NEIGH, NFNLGRP_NONE)){
      return -1;
    }
  }else{
    filter_netlink_dumper(dumpmsgs, dumpcount, RTM_GETNEIGH);
  }
  return 0;
}

struct nl_sock*
nl_socket_connect(int family){
  struct nl_sock *nls;
  if((nls = nl_socket_alloc()) == NULL){
    return NULL;
  }
  nl_socket_disable_seq_check(nls);
  if(nl_connect(nls, family)){
    nl_socket_free(nls);
    return NULL;
  }
  return nls;
}

static int
netstack_init(netstack* ns, const netstack_opts* opts){
  if(!validate_options(opts)){
    return -1;
  }
  // Get an initial dump of all entities, then updates via subscription.
  int dumpmsgs[] = {
    RTM_GETLINK,
    RTM_GETADDR,
    RTM_GETNEIGH,
    RTM_GETROUTE,
  };
  if(opts){
    memcpy(&ns->opts, opts, sizeof(*opts));
  }else{
    memset(&ns->opts, 0, sizeof(ns->opts));
  }
  if(ns->opts.diagfxn == NULL){
    ns->opts.diagfxn = null_diagfxn;
  }
  ns->nonce = 1;
  ns->dequeueidx = 0;
  ns->clear_to_send = true;
  ns->name_trie = NULL;
  ns->iface_count = 0;
  ns->iface_bytes = 0;
  memset(&ns->iface_hash, 0, sizeof(ns->iface_hash));
  if((ns->nl = nl_socket_connect(NETLINK_ROUTE)) == NULL){
    return -1;
  }
  int dumpercount = sizeof(dumpmsgs) / sizeof(*dumpmsgs);
  if(subscribe_to_netlink(ns, dumpmsgs, &dumpercount)){
    nl_socket_free(ns->nl);
    return -1;
  }
  if(ns->opts.initial_events != NETSTACK_INITIAL_EVENTS_NONE){
    memcpy(ns->txqueue, dumpmsgs, sizeof(dumpmsgs));
    ns->txqueue[dumpercount] = -1;
    ns->queueidx = dumpercount;
  }else{
    ns->txqueue[0] = -1;
    ns->queueidx = 0;
  }
  ns->netlink_errors = 0;
  ns->user_callbacks_total = 0;
  ns->lookup_copies = ns->lookup_shares = ns->lookup_failures = 0;
  ns->iface_events = ns->addr_events = ns->route_events = ns->neigh_events = 0;
  // Passes this netstack object to libnl. The nl_sock thus must be destroyed
  // before the netstack itself is.
  if(nl_socket_modify_cb(ns->nl, NL_CB_VALID, NL_CB_CUSTOM, msg_handler, ns)){
    nl_socket_free(ns->nl);
    return -1;
  }
  if(nl_socket_modify_err_cb(ns->nl, NL_CB_CUSTOM, err_handler, ns)){
    nl_socket_free(ns->nl);
    return -1;
  }
  if(pthread_mutex_init(&ns->hashlock, NULL)){
    nl_socket_free(ns->nl);
    return -1;
  }
  if(pthread_mutex_init(&ns->txlock, NULL)){
    pthread_mutex_destroy(&ns->hashlock);
    nl_socket_free(ns->nl);
    return -1;
  }
  if(pthread_cond_init(&ns->txcond, NULL)){
    pthread_mutex_destroy(&ns->txlock);
    pthread_mutex_destroy(&ns->hashlock);
    nl_socket_free(ns->nl);
    return -1;
  }
  if(pthread_create(&ns->rxtid, NULL, netstack_rx_thread, ns)){
    pthread_cond_destroy(&ns->txcond);
    pthread_mutex_destroy(&ns->txlock);
    pthread_mutex_destroy(&ns->hashlock);
    nl_socket_free(ns->nl);
    return -1;
  }
  if(pthread_create(&ns->txtid, NULL, netstack_tx_thread, ns)){
    pthread_cancel(ns->rxtid);
    pthread_join(ns->txtid, NULL);
    pthread_cond_destroy(&ns->txcond);
    pthread_mutex_destroy(&ns->txlock);
    pthread_mutex_destroy(&ns->hashlock);
    nl_socket_free(ns->nl);
    return -1;
  }
  if(ns->opts.initial_events == NETSTACK_INITIAL_EVENTS_BLOCK){
    pthread_mutex_lock(&ns->txlock);
    while(!ns->clear_to_send || ns->txqueue[ns->dequeueidx] != -1){
      pthread_cond_wait(&ns->txcond, &ns->txlock);
    }
    pthread_mutex_unlock(&ns->txlock);
  }
  return 0;
}

netstack* netstack_create(const netstack_opts* nopts){
  netstack* ns = malloc(sizeof(*ns));
  if(ns){
    if(netstack_init(ns, nopts)){
      free(ns);
      return NULL;
    }
  }
  return ns;
}

// Downref all netstack_iface objects remaining in our cache. This might not
// actually free them all; some might still be shared with the caller.
static void
destroy_iface_cache(netstack* ns){
  size_t z;
  for(z = 0 ; z < sizeof(ns->iface_hash) / sizeof(*ns->iface_hash) ; ++z){
    netstack_iface* ni = ns->iface_hash[z];
    while(ni){
      netstack_iface* tmp = ni->hnext;
      netstack_iface_destroy(ni);
      ni = tmp;
    }
  }
}

int netstack_destroy(netstack* ns){
  int ret = 0;
  if(ns){
    if(pthread_cancel(ns->rxtid) == 0 && pthread_cancel(ns->txtid) == 0){
      ret |= pthread_join(ns->txtid, NULL);
      ret |= pthread_join(ns->rxtid, NULL);
    }else{
      ret = -1;
    }
    nl_socket_free(ns->nl);
    ret |= pthread_cond_destroy(&ns->txcond);
    ret |= pthread_mutex_destroy(&ns->txlock);
    ret |= pthread_mutex_destroy(&ns->hashlock);
    destroy_iface_cache(ns);
    destroy_name_trie(ns->name_trie);
    free(ns);
  }
  return ret;
}

static netstack_iface*
netstack_iface_byname(const name_node* array, const char* name){
  while(array){
    if(!*name){
      return array->iface;
    }
    array = array->array[*(const unsigned char*)(name++)];
  }
  return NULL;
}

netstack_iface* netstack_iface_copy_byname(netstack* ns, const char* name){
  netstack_iface* ret;
  pthread_mutex_lock(&ns->hashlock);
  netstack_iface* ni = netstack_iface_byname(ns->name_trie, name);
  if(ni){
    ret = create_iface(ni->rtabuf, ni->rtabuflen);
  }else{
    ret = NULL;
  }
  pthread_mutex_unlock(&ns->hashlock);
  if(ret){
    ++ns->lookup_copies;
  }else{
    ++ns->lookup_failures;
  }
  return ret;
}

const netstack_iface* netstack_iface_share_byname(netstack* ns, const char* name){
  pthread_mutex_lock(&ns->hashlock);
  netstack_iface* ni = netstack_iface_byname(ns->name_trie, name);
  if(ni){
    atomic_fetch_add(&ni->refcount, 1);
  }
  pthread_mutex_unlock(&ns->hashlock);
  if(ni){
    atomic_fetch_add(&ns->lookup_shares, 1);
  }else{
    atomic_fetch_add(&ns->lookup_failures, 1);
  }
  return ni;
}

static inline netstack_iface*
netstack_iface_byidx(const netstack* ns, int idx){
  if(idx < 0){
    return NULL;
  }
  int hidx = iface_hash(ns, idx);
  netstack_iface* ni = ns->iface_hash[hidx];
  while(ni){
    if(ni->ifi.ifi_index == idx){
      break;
    }
    ni = ni->hnext;
  }
  return ni;
}

netstack_iface* netstack_iface_copy_byidx(netstack* ns, int idx){
  netstack_iface* ret;
  pthread_mutex_lock(&ns->hashlock);
  netstack_iface* ni = netstack_iface_byidx(ns, idx);
  if(ni){
    ret = create_iface(ni->rtabuf, ni->rtabuflen);
  }else{
    ret = NULL;
  }
  pthread_mutex_unlock(&ns->hashlock);
  if(ret){
    ++ns->lookup_copies;
  }else{
    ++ns->lookup_failures;
  }
  return ret;
}

// No locking, object is already owned by caller
netstack_iface* netstack_iface_copy(const netstack_iface* ni){
  return create_iface(ni->rtabuf, ni->rtabuflen);
}

const netstack_iface* netstack_iface_share_byidx(netstack* ns, int idx){
  pthread_mutex_lock(&ns->hashlock);
  netstack_iface* ni = netstack_iface_byidx(ns, idx);
  if(ni){
    atomic_fetch_add(&ni->refcount, 1);
  }
  pthread_mutex_unlock(&ns->hashlock);
  if(ni){
    atomic_fetch_add(&ns->lookup_shares, 1);
  }else{
    atomic_fetch_add(&ns->lookup_failures, 1);
  }
  return ni;
}

// Nothing gets locked here, since ownership indicates sufficient locking
const netstack_iface* netstack_iface_share(const netstack_iface* ni){
  netstack_iface* unsafe_ni = (netstack_iface*)ni;
  atomic_fetch_add(&unsafe_ni->refcount, 1);
  return ni;
}

void netstack_iface_abandon(const netstack_iface* ni){
  netstack_iface* unsafe_ni = (netstack_iface*)ni;
  netstack_iface_destroy(unsafe_ni);
}

uint64_t netstack_iface_bytes(const netstack* ns){
  netstack* unsafe_ns = (netstack*)ns;
  unsigned ret;
  pthread_mutex_lock(&unsafe_ns->hashlock);
  ret = ns->iface_bytes;
  pthread_mutex_unlock(&unsafe_ns->hashlock);
  return ret;
}

char* netstack_iface_qdisc(const struct netstack_iface* ni){
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_QDISC);
  if(rta == NULL){
    return NULL;
  }
  return strndup(RTA_DATA(rta), RTA_PAYLOAD(rta));
}

void netstack_iface_queuecounts(const struct netstack_iface* ni,
                                struct netstack_iface_qcounts* nqc){
  uint32_t val;
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_NUM_RX_QUEUES);
  nqc->rx = -1;
  if(rta && !rtattrtou32(rta, &val)){
    nqc->rx = val;
  }
  rta = netstack_iface_attr(ni, IFLA_NUM_TX_QUEUES);
  nqc->tx = -1;
  if(rta && !rtattrtou32(rta, &val)){
    nqc->tx = val;
  }
  nqc->combined = -1; // FIXME
  nqc->xdp = -1;      // FIXME
}

const struct rtattr* netstack_iface_attr(const netstack_iface* ni, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(ni->rta_index) / sizeof(*ni->rta_index)){
    return index_into_rta(ni->rtabuf, ni->rta_index[attridx]);
  }
  if(!ni->unknown_attrs){
    return NULL;
  }
  return netstack_extract_rta_attr(ni->rtabuf, ni->rtabuflen, attridx);
}

const struct rtattr* netstack_addr_attr(const netstack_addr* na, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(na->rta_index) / sizeof(*na->rta_index)){
    return index_into_rta(na->rtabuf, na->rta_index[attridx]);
  }
  if(!na->unknown_attrs){
    return NULL;
  }
  return netstack_extract_rta_attr(na->rtabuf, na->rtabuflen, attridx);
}

const struct rtattr* netstack_route_attr(const netstack_route* nr, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(nr->rta_index) / sizeof(*nr->rta_index)){
    return index_into_rta(nr->rtabuf, nr->rta_index[attridx]);
  }
  if(!nr->unknown_attrs){
    return NULL;
  }
  return netstack_extract_rta_attr(nr->rtabuf, nr->rtabuflen, attridx);
}

const struct rtattr* netstack_neigh_attr(const struct netstack_neigh* nn, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(nn->rta_index) / sizeof(*nn->rta_index)){
    return index_into_rta(nn->rtabuf, nn->rta_index[attridx]);
  }
  if(!nn->unknown_attrs){
    return NULL;
  }
  return netstack_extract_rta_attr(nn->rtabuf, nn->rtabuflen, attridx);
}

char* netstack_l3addrstr(int fam, const void* addr, char* str, size_t slen){
  if(!inet_ntop(fam, addr, str, slen)){
    return NULL;
  }
  return str;
}

char* netstack_iface_name(const netstack_iface* ni, char* name){
  return strcpy(name, ni->name);
}

unsigned netstack_iface_family(const netstack_iface* ni){
  return ni->ifi.ifi_family;
}

unsigned netstack_iface_type(const netstack_iface* ni){
  return ni->ifi.ifi_type;
}

char* netstack_iface_typestr(const netstack_iface* ni, char* buf, size_t blen){
  return nl_llproto2str(netstack_iface_type(ni), buf, blen);
}

int netstack_iface_index(const netstack_iface* ni){
  return ni->ifi.ifi_index;
}

unsigned netstack_neigh_family(const netstack_neigh* nn){
  return nn->nd.ndm_family;
}

int netstack_neigh_index(const netstack_neigh* nn){
  return nn->nd.ndm_ifindex;
}

unsigned netstack_neigh_flags(const netstack_neigh* nn){
  return nn->nd.ndm_flags;
}

unsigned netstack_neigh_type(const netstack_neigh* nn){
  return nn->nd.ndm_type;
}

unsigned netstack_neigh_state(const netstack_neigh* nn){
  return nn->nd.ndm_state;
}

unsigned netstack_addr_family(const netstack_addr* na){
  return na->ifa.ifa_family;
}

unsigned netstack_addr_prefixlen(const netstack_addr* na){
  return na->ifa.ifa_prefixlen;
}

unsigned netstack_addr_flags(const struct netstack_addr* na){
  return na->ifa.ifa_flags;
}

unsigned netstack_addr_scope(const struct netstack_addr* na){
  return na->ifa.ifa_scope;
}

int netstack_addr_index(const netstack_addr* na){
  return na->ifa.ifa_index;
}

char* netstack_addr_label(const struct netstack_addr* na){
  const struct rtattr* rta = netstack_addr_attr(na, IFA_LABEL);
  if(rta == NULL){
    return NULL;
  }
  return strndup(RTA_DATA(rta), RTA_PAYLOAD(rta));
}

unsigned netstack_route_family(const netstack_route* nr){
  return nr->rt.rtm_family;
}

unsigned netstack_route_dst_len(const netstack_route* nr){
  return nr->rt.rtm_dst_len;
}

unsigned netstack_route_src_len(const netstack_route* nr){
  return nr->rt.rtm_src_len;
}

unsigned netstack_route_tos(const netstack_route* nr){
  return nr->rt.rtm_tos;
}

unsigned netstack_route_table(const netstack_route* nr){
  return nr->rt.rtm_table;
}

unsigned netstack_route_protocol(const netstack_route* nr){
  return nr->rt.rtm_protocol;
}

unsigned netstack_route_scope(const struct netstack_route* nr){
  return nr->rt.rtm_scope;
}

unsigned netstack_route_type(const netstack_route* nr){
  return nr->rt.rtm_type;
}

unsigned netstack_route_flags(const netstack_route* nr){
  return nr->rt.rtm_flags;
}

char* netstack_l2addrstr(unsigned l2type, size_t len, const void* addr){
  (void)l2type; // FIXME need for quirks
  // Each byte becomes two ASCII characters + separator or nul
  size_t slen = len == 0 ? 1 : len * 3;
  char* ret = (char*)malloc(slen);
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

netstack_stats* netstack_sample_stats(const netstack* ns, netstack_stats* stats){
  netstack* unsafe_ns = (netstack*)ns;
  stats->netlink_errors = ns->netlink_errors;
  stats->user_callbacks_total = ns->user_callbacks_total;
  stats->lookup_copies = ns->lookup_copies;
  stats->lookup_shares = ns->lookup_shares;
  stats->lookup_failures = ns->lookup_failures;
  stats->iface_events = ns->iface_events;
  stats->addr_events = ns->addr_events;
  stats->route_events = ns->route_events;
  stats->neigh_events = ns->neigh_events;
  pthread_mutex_lock(&unsafe_ns->hashlock);
  stats->ifaces = ns->iface_count;
  pthread_mutex_unlock(&unsafe_ns->hashlock);
  // FIXME not yet maintained
  stats->addrs = 0;
  stats->routes = 0;
  stats->neighs = 0;
  stats->zombie_shares = 0;
  return stats;
}
