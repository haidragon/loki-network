#include <dht/recursiverouterlookup.hpp>

#include <dht/context.hpp>
#include <dht/messages/findrouter.hpp>
#include <dht/messages/gotrouter.hpp>

namespace llarp
{
  namespace dht
  {
    RecursiveRouterLookup::RecursiveRouterLookup(const TXOwner &whoasked,
                                                 const RouterID &target,
                                                 AbstractContext *ctx,
                                                 RouterLookupHandler result)
        : TX< RouterID, RouterContact >(whoasked, target, ctx)
        , resultHandler(result)

    {
      peersAsked.insert(ctx->OurKey());
    }

    bool
    RecursiveRouterLookup::Validate(const RouterContact &rc) const
    {
      if(!rc.Verify(parent->Crypto(), parent->Now()))
      {
        llarp::LogWarn("rc from lookup result is invalid");
        return false;
      }
      return true;
    }

    void
    RecursiveRouterLookup::Start(const TXOwner &peer)
    {
      parent->DHTSendTo(peer.node.as_array(),
                        new FindRouterMessage(peer.txid, target));
    }

    void
    RecursiveRouterLookup::SendReply()
    {
      if(valuesFound.size())
      {
        RouterContact found;
        for(const auto &rc : valuesFound)
        {
          if(found.OtherIsNewer(rc))
            found = rc;
        }
        valuesFound.clear();
        valuesFound.emplace_back(found);
      }
      if(resultHandler)
      {
        resultHandler(valuesFound);
      }
      if(whoasked.node != parent->OurKey())
        parent->DHTSendTo(
            whoasked.node.as_array(),
            new GotRouterMessage({}, whoasked.txid, valuesFound, false), false);
    }
  }  // namespace dht
}  // namespace llarp
