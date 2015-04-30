/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __MASTER_ALLOCATOR_MESOS_HIERARCHICAL_HPP__
#define __MASTER_ALLOCATOR_MESOS_HIERARCHICAL_HPP__

#include <algorithm>
#include <vector>

#include <mesos/resources.hpp>
#include <mesos/type_utils.hpp>

#include <process/delay.hpp>
#include <process/id.hpp>
#include <process/timeout.hpp>

#include <stout/check.hpp>
#include <stout/duration.hpp>
#include <stout/hashmap.hpp>
#include <stout/stopwatch.hpp>
#include <stout/stringify.hpp>

#include "mesos/allocator.hpp"
#include "sorter/drf/sorter.hpp"

namespace mesos {
namespace internal {
namespace master {
namespace allocator {

// Forward declarations.
class Filter;


// We forward declare the hierarchical allocator process so that we
// can typedef an instantiation of it with DRF sorters.
template <typename RoleSorter, typename FrameworkSorter>
class HierarchicalAllocatorProcess;

typedef HierarchicalAllocatorProcess<DRFSorter, DRFSorter>
HierarchicalDRFAllocatorProcess;

typedef MesosAllocator<HierarchicalDRFAllocatorProcess>
HierarchicalDRFAllocator;


// Implements the basic allocator algorithm - first pick a role by
// some criteria, then pick one of their frameworks to allocate to.
template <typename RoleSorter, typename FrameworkSorter>
class HierarchicalAllocatorProcess : public MesosAllocatorProcess
{
public:
  HierarchicalAllocatorProcess();

  virtual ~HierarchicalAllocatorProcess();

  process::PID<HierarchicalAllocatorProcess> self();

  void initialize(
      const Duration& allocationInterval,
      const lambda::function<
          void(const FrameworkID&,
               const hashmap<SlaveID, Resources>&)>& offerCallback,
      const hashmap<std::string, mesos::master::RoleInfo>& roles);

  void addFramework(
      const FrameworkID& frameworkId,
      const FrameworkInfo& frameworkInfo,
      const hashmap<SlaveID, Resources>& used);

  void removeFramework(
      const FrameworkID& frameworkId);

  void activateFramework(
      const FrameworkID& frameworkId);

  void deactivateFramework(
      const FrameworkID& frameworkId);

  void addSlave(
      const SlaveID& slaveId,
      const SlaveInfo& slaveInfo,
      const Resources& total,
      const hashmap<FrameworkID, Resources>& used);

  void removeSlave(
      const SlaveID& slaveId);

  void deactivateSlave(
      const SlaveID& slaveId);

  void activateSlave(
      const SlaveID& slaveId);

  void updateWhitelist(
      const Option<hashset<std::string> >& whitelist);

  void requestResources(
      const FrameworkID& frameworkId,
      const std::vector<Request>& requests);

  void updateAllocation(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const std::vector<Offer::Operation>& operations);

  void recoverResources(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const Resources& resources,
      const Option<Filters>& filters);

  void reviveOffers(
      const FrameworkID& frameworkId);

protected:
  // Useful typedefs for dispatch/delay/defer to self()/this.
  typedef HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter> Self;
  typedef HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter> This;

  // Callback for doing batch allocations.
  void batch();

  // Allocate any allocatable resources.
  void allocate();

  // Allocate resources just from the specified slave.
  void allocate(const SlaveID& slaveId);

  // Allocate resources from the specified slaves.
  void allocate(const hashset<SlaveID>& slaveIds);

  // Remove a filter for the specified framework.
  void expire(const FrameworkID& frameworkId, Filter* filter);

  // Checks whether the slave is whitelisted.
  bool isWhitelisted(const SlaveID& slaveId);

  // Returns true if there is a filter for this framework
  // on this slave.
  bool isFiltered(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const Resources& resources);

  bool allocatable(const Resources& resources);

  bool initialized;

  Duration allocationInterval;

  lambda::function<
      void(const FrameworkID&,
           const hashmap<SlaveID, Resources>&)> offerCallback;

  struct Framework
  {
    std::string role;
    bool checkpoint;  // Whether the framework desires checkpointing.

    hashset<Filter*> filters; // Active filters for the framework.
  };

  hashmap<FrameworkID, Framework> frameworks;

  struct Slave
  {
    Resources total;
    Resources available;

    bool activated;  // Whether to offer resources.
    bool checkpoint; // Whether slave supports checkpointing.

    std::string hostname;
  };

  hashmap<SlaveID, Slave> slaves;

  hashmap<std::string, mesos::master::RoleInfo> roles;

  // Slaves to send offers for.
  Option<hashset<std::string> > whitelist;

  // There are two levels of sorting, hence "hierarchical".
  // Level 1 sorts across roles:
  //   Reserved resources are excluded from fairness calculation,
  //   since they are forcibly pinned to a role.
  // Level 2 sorts across frameworks within a particular role:
  //   Both reserved resources and unreserved resources are used
  //   in the fairness calculation. This is because reserved
  //   resources can be allocated to any framework in the role.
  RoleSorter* roleSorter;
  hashmap<std::string, FrameworkSorter*> frameworkSorters;
};


// Used to represent "filters" for resources unused in offers.
class Filter
{
public:
  virtual ~Filter() {}

  virtual bool filter(const SlaveID& slaveId, const Resources& resources) = 0;
};


class RefusedFilter: public Filter
{
public:
  RefusedFilter(
      const SlaveID& _slaveId,
      const Resources& _resources,
      const process::Timeout& _timeout)
    : slaveId(_slaveId), resources(_resources), timeout(_timeout) {}

  virtual bool filter(const SlaveID& _slaveId, const Resources& _resources)
  {
    return slaveId == _slaveId &&
           resources.contains(_resources) && // Refused resources are superset.
           timeout.remaining() > Seconds(0);
  }

  const SlaveID slaveId;
  const Resources resources;
  const process::Timeout timeout;
};


template <class RoleSorter, class FrameworkSorter>
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::HierarchicalAllocatorProcess() // NOLINT(whitespace/line_length)
  : ProcessBase(process::ID::generate("hierarchical-allocator")),
    initialized(false) {}


template <class RoleSorter, class FrameworkSorter>
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::~HierarchicalAllocatorProcess() // NOLINT(whitespace/line_length)
{}


template <class RoleSorter, class FrameworkSorter>
process::PID<HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter> >
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::self()
{
  return process::PID<HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter> >(this); // NOLINT(whitespace/line_length)
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::initialize(
    const Duration& _allocationInterval,
    const lambda::function<
        void(const FrameworkID&,
             const hashmap<SlaveID, Resources>&)>& _offerCallback,
    const hashmap<std::string, mesos::master::RoleInfo>& _roles)
{
  allocationInterval = _allocationInterval;
  offerCallback = _offerCallback;
  roles = _roles;
  initialized = true;

  roleSorter = new RoleSorter();
  foreachpair (
      const std::string& name, const mesos::master::RoleInfo& roleInfo, roles) {
    roleSorter->add(name, roleInfo.weight());
    frameworkSorters[name] = new FrameworkSorter();
  }

  if (roleSorter->count() == 0) {
    LOG(ERROR) << "No roles specified, cannot allocate resources!";
  }

  VLOG(1) << "Initialized hierarchical allocator process";

  delay(allocationInterval, self(), &Self::batch);
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::addFramework(
    const FrameworkID& frameworkId,
    const FrameworkInfo& frameworkInfo,
    const hashmap<SlaveID, Resources>& used_)
{
  CHECK(initialized);

  const std::string& role = frameworkInfo.role();

  CHECK(roles.contains(role));

  CHECK(!frameworkSorters[role]->contains(frameworkId.value()));
  frameworkSorters[role]->add(frameworkId.value());

  // TODO(bmahler): Validate that the reserved resources have the
  // framework's role.

  // Update the allocation to this framework.
  // TODO(mpark): Once the sorter API is updated to operate on
  // 'hashmap<SlaveID, Resources>' rather than 'Resources', update
  // the sorters for each slave instead.
  Resources used = Resources::sum(used_);
  roleSorter->allocated(role, used.unreserved());
  frameworkSorters[role]->add(used);
  frameworkSorters[role]->allocated(frameworkId.value(), used);

  frameworks[frameworkId] = Framework();
  frameworks[frameworkId].role = frameworkInfo.role();
  frameworks[frameworkId].checkpoint = frameworkInfo.checkpoint();

  LOG(INFO) << "Added framework " << frameworkId;

  allocate();
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::removeFramework(
    const FrameworkID& frameworkId)
{
  CHECK(initialized);

  CHECK(frameworks.contains(frameworkId));
  const std::string& role = frameworks[frameworkId].role;

  // Might not be in 'frameworkSorters[role]' because it was previously
  // deactivated and never re-added.
  if (frameworkSorters[role]->contains(frameworkId.value())) {
    Resources allocation =
      frameworkSorters[role]->allocation(frameworkId.value());

    roleSorter->unallocated(role, allocation.unreserved());
    frameworkSorters[role]->remove(allocation);
    frameworkSorters[role]->remove(frameworkId.value());
  }

  // Do not delete the filters contained in this
  // framework's 'filters' hashset yet, see comments in
  // HierarchicalAllocatorProcess::reviveOffers and
  // HierarchicalAllocatorProcess::expire.
  frameworks.erase(frameworkId);

  LOG(INFO) << "Removed framework " << frameworkId;
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::activateFramework(
    const FrameworkID& frameworkId)
{
  CHECK(initialized);

  CHECK(frameworks.contains(frameworkId));
  const std::string& role = frameworks[frameworkId].role;

  frameworkSorters[role]->activate(frameworkId.value());

  LOG(INFO) << "Activated framework " << frameworkId;

  allocate();
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::deactivateFramework(
    const FrameworkID& frameworkId)
{
  CHECK(initialized);

  CHECK(frameworks.contains(frameworkId));
  const std::string& role = frameworks[frameworkId].role;

  frameworkSorters[role]->deactivate(frameworkId.value());

  // Note that the Sorter *does not* remove the resources allocated
  // to this framework. For now, this is important because if the
  // framework fails over and is activated, we still want a record
  // of the resources that it is using. We might be able to collapse
  // the added/removed and activated/deactivated in the future.

  // Do not delete the filters contained in this
  // framework's 'filters' hashset yet, see comments in
  // HierarchicalAllocatorProcess::reviveOffers and
  // HierarchicalAllocatorProcess::expire.
  frameworks[frameworkId].filters.clear();

  LOG(INFO) << "Deactivated framework " << frameworkId;
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::addSlave(
    const SlaveID& slaveId,
    const SlaveInfo& slaveInfo,
    const Resources& total,
    const hashmap<FrameworkID, Resources>& used)
{
  CHECK(initialized);
  CHECK(!slaves.contains(slaveId));

  roleSorter->add(total.unreserved());

  foreachpair (const FrameworkID& frameworkId,
               const Resources& allocated,
               used) {
    if (frameworks.contains(frameworkId)) {
      const std::string& role = frameworks[frameworkId].role;

      // TODO(bmahler): Validate that the reserved resources have the
      // framework's role.

      roleSorter->allocated(role, allocated.unreserved());
      frameworkSorters[role]->add(allocated);
      frameworkSorters[role]->allocated(frameworkId.value(), allocated);
    }
  }

  slaves[slaveId] = Slave();
  slaves[slaveId].total = total;
  slaves[slaveId].available = total - Resources::sum(used);
  slaves[slaveId].activated = true;
  slaves[slaveId].checkpoint = slaveInfo.checkpoint();
  slaves[slaveId].hostname = slaveInfo.hostname();

  LOG(INFO) << "Added slave " << slaveId << " (" << slaves[slaveId].hostname
            << ") with " << slaves[slaveId].total
            << " (and " << slaves[slaveId].available << " available)";

  allocate(slaveId);
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::removeSlave(
    const SlaveID& slaveId)
{
  CHECK(initialized);
  CHECK(slaves.contains(slaveId));

  // TODO(bmahler): Per MESOS-621, this should remove the allocations
  // that any frameworks have on this slave. Otherwise the caller may
  // "leak" allocated resources accidentally if they forget to recover
  // all the resources. Fixing this would require more information
  // than what we currently track in the allocator.

  roleSorter->remove(slaves[slaveId].total.unreserved());

  slaves.erase(slaveId);

  // Note that we DO NOT actually delete any filters associated with
  // this slave, that will occur when the delayed
  // HierarchicalAllocatorProcess::expire gets invoked (or the framework
  // that applied the filters gets removed).

  LOG(INFO) << "Removed slave " << slaveId;
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::activateSlave(
    const SlaveID& slaveId)
{
  CHECK(initialized);
  CHECK(slaves.contains(slaveId));

  slaves[slaveId].activated = true;

  LOG(INFO)<< "Slave " << slaveId << " reactivated";
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::deactivateSlave(
    const SlaveID& slaveId)
{
  CHECK(initialized);
  CHECK(slaves.contains(slaveId));

  slaves[slaveId].activated = false;

  LOG(INFO) << "Slave " << slaveId << " deactivated";
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::updateWhitelist(
    const Option<hashset<std::string> >& _whitelist)
{
  CHECK(initialized);

  whitelist = _whitelist;

  if (whitelist.isSome()) {
    LOG(INFO) << "Updated slave whitelist: " << stringify(whitelist.get());

    if (whitelist.get().empty()) {
      LOG(WARNING) << "Whitelist is empty, no offers will be made!";
    }
  } else {
    LOG(INFO) << "Advertising offers for all slaves";
  }
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::requestResources(
    const FrameworkID& frameworkId,
    const std::vector<Request>& requests)
{
  CHECK(initialized);

  LOG(INFO) << "Received resource request from framework " << frameworkId;
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::updateAllocation(
    const FrameworkID& frameworkId,
    const SlaveID& slaveId,
    const std::vector<Offer::Operation>& operations)
{
  CHECK(initialized);
  CHECK(slaves.contains(slaveId));
  CHECK(frameworks.contains(frameworkId));

  // The total resources on the slave are composed of both allocated
  // and available resources:
  //
  //    total = available + allocated
  //
  // Here we apply offer operations to the allocated resources, which
  // in turns leads to an update of the total. The available resources
  // remain unchanged.

  FrameworkSorter* frameworkSorter =
    frameworkSorters[frameworks[frameworkId].role];

  Resources allocation = frameworkSorter->allocation(frameworkId.value());

  // Update the allocated resources.
  Try<Resources> updatedAllocation = allocation.apply(operations);
  CHECK_SOME(updatedAllocation);

  frameworkSorter->update(
      frameworkId.value(),
      allocation,
      updatedAllocation.get());

  roleSorter->update(
      frameworks[frameworkId].role,
      allocation.unreserved(),
      updatedAllocation.get().unreserved());

  // Update the total resources.
  Try<Resources> updatedTotal = slaves[slaveId].total.apply(operations);
  CHECK_SOME(updatedTotal);

  slaves[slaveId].total = updatedTotal.get();

  // TODO(bmahler): Validate that the available resources are
  // unaffected. This requires augmenting the sorters with
  // SlaveIDs for allocations, so that we can do:
  //
  //   CHECK_EQ(slaves[slaveId].total - updatedAllocation,
  //            slaves[slaveId].available);

  // TODO(jieyu): Do not log if there is no update.
  LOG(INFO) << "Updated allocation of framework " << frameworkId
            << " on slave " << slaveId
            << " from " << allocation << " to " << updatedAllocation.get();
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::recoverResources(
    const FrameworkID& frameworkId,
    const SlaveID& slaveId,
    const Resources& resources,
    const Option<Filters>& filters)
{
  CHECK(initialized);

  if (resources.empty()) {
    return;
  }

  // Updated resources allocated to framework (if framework still
  // exists, which it might not in the event that we dispatched
  // Master::offer before we received
  // MesosAllocatorProcess::removeFramework or
  // MesosAllocatorProcess::deactivateFramework, in which case we will
  // have already recovered all of its resources).
  if (frameworks.contains(frameworkId)) {
    const std::string& role = frameworks[frameworkId].role;

    CHECK(frameworkSorters.contains(role));

    if (frameworkSorters[role]->contains(frameworkId.value())) {
      frameworkSorters[role]->unallocated(frameworkId.value(), resources);
      frameworkSorters[role]->remove(resources);
      roleSorter->unallocated(role, resources.unreserved());
    }
  }

  // Update resources allocatable on slave (if slave still exists,
  // which it might not in the event that we dispatched Master::offer
  // before we received Allocator::removeSlave).
  if (slaves.contains(slaveId)) {
    slaves[slaveId].available += resources;

    LOG(INFO) << "Recovered " << resources
              << " (total allocatable: " << slaves[slaveId].available
              << ") on slave " << slaveId
              << " from framework " << frameworkId;
  }

  // No need to install the filter if 'filters' is none.
  if (filters.isNone()) {
    return;
  }

  // No need to install the filter if slave/framework does not exist.
  if (!frameworks.contains(frameworkId) || !slaves.contains(slaveId)) {
    return;
  }

  // Create a refused resources filter.
  Try<Duration> seconds = Duration::create(filters.get().refuse_seconds());

  if (seconds.isError()) {
    LOG(WARNING) << "Using the default value of 'refuse_seconds' to create "
                 << "the refused resources filter because the input value "
                 << "is invalid: " << seconds.error();

    seconds = Duration::create(Filters().refuse_seconds());
  } else if (seconds.get() < Duration::zero()) {
    LOG(WARNING) << "Using the default value of 'refuse_seconds' to create "
                 << "the refused resources filter because the input value "
                 << "is negative";

    seconds = Duration::create(Filters().refuse_seconds());
  }

  CHECK_SOME(seconds);

  if (seconds.get() != Duration::zero()) {
    VLOG(1) << "Framework " << frameworkId
            << " filtered slave " << slaveId
            << " for " << seconds.get();

    // Create a new filter and delay its expiration.
    Filter* filter = new RefusedFilter(
        slaveId,
        resources,
        process::Timeout::in(seconds.get()));

    frameworks[frameworkId].filters.insert(filter);

    delay(seconds.get(), self(), &Self::expire, frameworkId, filter);
  }
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::reviveOffers(
    const FrameworkID& frameworkId)
{
  CHECK(initialized);

  frameworks[frameworkId].filters.clear();

  // We delete each actual Filter when
  // HierarchicalAllocatorProcess::expire gets invoked. If we delete the
  // Filter here it's possible that the same Filter (i.e., same
  // address) could get reused and HierarchicalAllocatorProcess::expire
  // would expire that filter too soon. Note that this only works
  // right now because ALL Filter types "expire".

  LOG(INFO) << "Removed filters for framework " << frameworkId;

  allocate();
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::batch()
{
  allocate();
  delay(allocationInterval, self(), &Self::batch);
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::allocate()
{
  Stopwatch stopwatch;
  stopwatch.start();

  allocate(slaves.keys());

  VLOG(1) << "Performed allocation for " << slaves.size() << " slaves in "
            << stopwatch.elapsed();
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::allocate(
    const SlaveID& slaveId)
{
  Stopwatch stopwatch;
  stopwatch.start();

  // TODO(bmahler): Add initializer list constructor for hashset.
  hashset<SlaveID> slaves;
  slaves.insert(slaveId);
  allocate(slaves);

  VLOG(1) << "Performed allocation for slave " << slaveId << " in "
          << stopwatch.elapsed();
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::allocate(
    const hashset<SlaveID>& slaveIds_)
{
  if (roleSorter->count() == 0) {
    LOG(ERROR) << "No roles specified, cannot allocate resources!";
    return;
  }

  // Compute the offerable resources, per framework:
  //   (1) For reserved resources on the slave, allocate these to a
  //       framework having the corresponding role.
  //   (2) For unreserved resources on the slave, allocate these
  //       to a framework of any role.
  hashmap<FrameworkID, hashmap<SlaveID, Resources> > offerable;

  // Randomize the order in which slaves' resources are allocated.
  // TODO(vinod): Implement a smarter sorting algorithm.
  std::vector<SlaveID> slaveIds(slaveIds_.begin(), slaveIds_.end());
  std::random_shuffle(slaveIds.begin(), slaveIds.end());

  foreach (const SlaveID& slaveId, slaveIds) {
    // Don't send offers for non-whitelisted and deactivated slaves.
    if (!isWhitelisted(slaveId) || !slaves[slaveId].activated) {
      continue;
    }

    foreach (const std::string& role, roleSorter->sort()) {
      foreach (const std::string& frameworkId_,
               frameworkSorters[role]->sort()) {
        FrameworkID frameworkId;
        frameworkId.set_value(frameworkId_);

        // NOTE: Currently, frameworks are allowed to have '*' role.
        // Calling reserved('*') returns an empty Resources object.
        Resources resources =
          slaves[slaveId].available.unreserved() +
          slaves[slaveId].available.reserved(role);

        // If the resources are not allocatable, ignore.
        if (!allocatable(resources)) {
          continue;
        }

        // If the framework filters these resources, ignore.
        if (isFiltered(frameworkId, slaveId, resources)) {
          continue;
        }

        VLOG(2) << "Allocating " << resources << " on slave " << slaveId
                << " to framework " << frameworkId;

        // Note that we perform "coarse-grained" allocation,
        // meaning that we always allocate the entire remaining
        // slave resources to a single framework.
        offerable[frameworkId][slaveId] = resources;
        slaves[slaveId].available -= resources;

        // Reserved resources are only accounted for in the framework
        // sorter, since the reserved resources are not shared across
        // roles.
        frameworkSorters[role]->add(resources);
        frameworkSorters[role]->allocated(frameworkId_, resources);
        roleSorter->allocated(role, resources.unreserved());
      }
    }
  }

  if (offerable.empty()) {
    VLOG(1) << "No resources available to allocate!";
  } else {
    // Now offer the resources to each framework.
    foreachkey (const FrameworkID& frameworkId, offerable) {
      offerCallback(frameworkId, offerable[frameworkId]);
    }
  }
}


template <class RoleSorter, class FrameworkSorter>
void
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::expire(
    const FrameworkID& frameworkId,
    Filter* filter)
{
  // The filter might have already been removed (e.g., if the
  // framework no longer exists or in
  // HierarchicalAllocatorProcess::reviveOffers) but not yet deleted (to
  // keep the address from getting reused possibly causing premature
  // expiration).
  if (frameworks.contains(frameworkId) &&
      frameworks[frameworkId].filters.contains(filter)) {
    frameworks[frameworkId].filters.erase(filter);
  }

  delete filter;
}


template <class RoleSorter, class FrameworkSorter>
bool
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::isWhitelisted(
    const SlaveID& slaveId)
{
  CHECK(slaves.contains(slaveId));

  return whitelist.isNone() ||
         whitelist.get().contains(slaves[slaveId].hostname);
}


template <class RoleSorter, class FrameworkSorter>
bool
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::isFiltered(
    const FrameworkID& frameworkId,
    const SlaveID& slaveId,
    const Resources& resources)
{
  CHECK(frameworks.contains(frameworkId));
  CHECK(slaves.contains(slaveId));

  // Do not offer a non-checkpointing slave's resources to a checkpointing
  // framework. This is a short term fix until the following is resolved:
  // https://issues.apache.org/jira/browse/MESOS-444.
  if (frameworks[frameworkId].checkpoint && !slaves[slaveId].checkpoint) {
    VLOG(1) << "Filtered " << resources
            << " on non-checkpointing slave " << slaveId
            << " for checkpointing framework " << frameworkId;
    return true;
  }

  foreach (Filter* filter, frameworks[frameworkId].filters) {
    if (filter->filter(slaveId, resources)) {
      VLOG(1) << "Filtered " << resources
              << " on slave " << slaveId
              << " for framework " << frameworkId;
      return true;
    }
  }
  return false;
}


template <class RoleSorter, class FrameworkSorter>
bool
HierarchicalAllocatorProcess<RoleSorter, FrameworkSorter>::allocatable(
    const Resources& resources)
{
  Option<double> cpus = resources.cpus();
  Option<Bytes> mem = resources.mem();

  return (cpus.isSome() && cpus.get() >= MIN_CPUS) ||
         (mem.isSome() && mem.get() >= MIN_MEM);
}

} // namespace allocator {
} // namespace master {
} // namespace internal {
} // namespace mesos {

#endif // __MASTER_ALLOCATOR_MESOS_HIERARCHICAL_HPP__
