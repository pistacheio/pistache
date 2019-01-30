/* tcp.cc
   Mathieu Stefani, 05 novembre 2015

   TCP
*/

#include <pistache/tcp.h>
#include <pistache/peer.h>

namespace Pistache {
namespace Tcp {

Handler::Handler()
    : transport_(nullptr)
{
}

Handler::~Handler()
{ }

void
Handler::associateTransport(Transport* transport) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    transport_ = transport;
}

void
Handler::onConnection(const std::shared_ptr<Tcp::Peer>& peer) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    UNUSED(peer)
}

void
Handler::onDisconnection(const std::shared_ptr<Tcp::Peer>& peer) {
std::cout << __PRETTY_FUNCTION__ << std::endl;
    UNUSED(peer)
}

} // namespace Tcp
} // namespace Pistache
