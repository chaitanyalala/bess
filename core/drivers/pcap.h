#pragma once
#include <errno.h>
#include <pcap/pcap.h>

#include <glog/logging.h>

#include "../message.h"
#include "../port.h"
#include "../snobj.h"
#include "../utils/pcap.h"

/*!
 * Queue of data to be transmitted. 
 * TODO: Why is this global and not declared on the stack?
 */
static unsigned char tx_pcap_data[PCAP_SNAPLEN];

/*!
 * Port to connect to a device via PCAP.
 * (Not recommended because PCAP is slow :-)
 * This driver is experimental. Currently does not support mbuf chaining and
 * needs more tests!
 */
class PCAPPort : public Port {
 public:
  PCAPPort() : Port(), pcap_handle_() {}

  /*!
   * Initialize the PCAP driver.
   *
   * PARAMETERS:
   * * string dev : the device to bind to with PCAP.
   */
   virtual pb_error_t Init(const bess::PCAPPortArg &arg);
  
  /*!
   * Deprecated! Don't use!
   */
  virtual struct snobj *Init(struct snobj *arg);

  /*!
   * Releases the PCAP binding to the device.
   */
  virtual void DeInit();

  /*!
   * Sends packets out on the device.
   * 
   * PARAMETERS:
   * * queue_t quid : PCAP has no notion of queues so this is ignored.
   * * snb_array_t pkts   : packets to transmit.
   * * int cnt  : number of packets in pkts to transmit.
   *
   * EXPECTS:
   * * Only call this after calling Init with a device.
   * * Don't call this after calling DeInit().
   *
   * RETURNS:
   * * Total number of packets sent (<=cnt).
   */
  virtual int SendPackets(queue_t qid, snb_array_t pkts, int cnt);
 
  /*!
   * Receives packets from the device.
   * 
   * PARAMETERS:
   * * queue_t quid : PCAP has no notion of queues so this is ignored.
   * * snb_array_t pkts   : buffer to store received packets in to.
   * * int cnt  : max number of packets to pull.
   *
   * EXPECTS:
   * * Only call this after calling Init with a device.
   * * Don't call this after calling DeInit().
   *
   * RETURNS:
   * * Total number of packets received (<=cnt)
   */ 
  virtual int RecvPackets(queue_t qid, snb_array_t pkts, int cnt);
  
 private:
  /*!
   * If sending and mbuf chain, need to reformat to a buffer of packets to be
   * compatible with what PCAP expects. This helper function copies mbuf packets
   * in to such a buffer.
   * 
   * PARAMTERS:
   * * unsigned char* data  : output buffer of pcap-compatible packets.
   * * struct rte_mbuf* mbuf  : mbuf chain to copy to output buffer.*/ 
  static void GatherData(unsigned char *data, struct rte_mbuf *mbuf);

  /*!
   * PCAP handle to device. 
   * CREATE: Init()
   * DESTROY: DeInit()
   */
  pcap_t *pcap_handle_;
};
