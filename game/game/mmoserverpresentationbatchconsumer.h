#pragma once

#include <cstddef>

#include "mmoserverpresentationmailbox.h"
#include "mmoserverpresentationstate.h"

namespace Mmo::ClientPresentation {

struct ServerPresentationBatchConsumeStats final {
  std::size_t sourceRejected = 0U;
  std::size_t routesApplied = 0U;
  std::size_t bootstrapsApplied = 0U;
  std::size_t eventsApplied = 0U;
  std::size_t stateRejected = 0U;
};

namespace Detail {

template<class Sink>
void notifyRouteApplied(
    Sink& sink,
    const ServerPresentationRouteIdentity& route,
    const ServerPresentationRouteReplaceResult& result) {
  if constexpr(requires { sink.routeApplied(route, result); })
    sink.routeApplied(route, result);
}

template<class Sink>
void notifyBootstrapApplied(
    Sink& sink,
    const ServerPresentationBootstrap& bootstrap,
    const ServerPresentationBootstrapInstallResult& result) {
  if constexpr(requires { sink.bootstrapApplied(bootstrap, result); })
    sink.bootstrapApplied(bootstrap, result);
}

template<class Sink>
void notifyEventApplied(
    Sink& sink,
    const ServerPresentationEvent& event,
    const ServerPresentationApplyResult& result) {
  if constexpr(requires { sink.eventApplied(event, result); })
    sink.eventApplied(event, result);
}

template<class Sink, class Record, class Result>
void notifyRejected(Sink& sink, const Record& record, const Result& result) {
  if constexpr(requires { sink.rejected(record, result); })
    sink.rejected(record, result);
}

template<class Sink>
void notifySourceRejected(Sink& sink, const std::size_t count) {
  if constexpr(requires { sink.sourceRejected(count); })
    sink.sourceRejected(count);
}

} // namespace Detail

// Applies a facade mailbox cut in its authority order: route, baseline, live
// events. The sink is notified only after state validation succeeds, so engine
// presentation code cannot observe a route, baseline or mutation that the
// typed state rejected.
template<class Sink>
[[nodiscard]] ServerPresentationBatchConsumeStats consumeServerPresentationBatch(
    ServerPresentationState& state,
    const ServerPresentationMailboxBatch& batch,
    Sink& sink) {
  ServerPresentationBatchConsumeStats stats;
  stats.sourceRejected = batch.rejectedRecords;
  if(batch.rejectedRecords != 0U)
    Detail::notifySourceRejected(sink, batch.rejectedRecords);

  if(batch.route.has_value()) {
    const auto result = state.replaceRoute(*batch.route);
    if(result.applied()) {
      ++stats.routesApplied;
      Detail::notifyRouteApplied(sink, *batch.route, result);
    } else if(result.status != ServerPresentationApplyStatus::Duplicate) {
      ++stats.stateRejected;
      Detail::notifyRejected(sink, *batch.route, result);
    }
  }

  for(const auto& bootstrap : batch.bootstraps) {
    const auto result = state.installBootstrap(bootstrap);
    if(result.applied()) {
      ++stats.bootstrapsApplied;
      Detail::notifyBootstrapApplied(sink, bootstrap, result);
    } else if(result.status != ServerPresentationApplyStatus::Duplicate &&
              result.status != ServerPresentationApplyStatus::Stale) {
      ++stats.stateRejected;
      Detail::notifyRejected(sink, bootstrap, result);
    }
  }

  for(const auto& event : batch.events) {
    const auto result = state.apply(event);
    if(result.applied()) {
      ++stats.eventsApplied;
      Detail::notifyEventApplied(sink, event, result);
    } else if(result.status != ServerPresentationApplyStatus::Duplicate &&
              result.status != ServerPresentationApplyStatus::Stale) {
      ++stats.stateRejected;
      Detail::notifyRejected(sink, event, result);
    }
  }
  return stats;
}

} // namespace Mmo::ClientPresentation
