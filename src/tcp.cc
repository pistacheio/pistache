/* tcp.cc
   Mathieu Stefani, 05 novembre 2015
   
   TCP
*/

#include "tcp.h"
#include "peer.h"

namespace Net {

namespace Tcp {

Handler::Handler()
{ }

Handler::~Handler()
{ }

void
Handler::onConnection(const std::shared_ptr<Tcp::Peer>& peer) {
}

void
Handler::onDisconnection(const std::shared_ptr<Tcp::Peer>& peer) {
}

} // namespace Tcp

} // namespace Net
