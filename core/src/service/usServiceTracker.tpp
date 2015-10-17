/*=============================================================================

  Library: CppMicroServices

  Copyright (c) The CppMicroServices developers. See the COPYRIGHT
  file at the top-level directory of this distribution and at
  https://github.com/saschazelzer/CppMicroServices/COPYRIGHT .

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

=============================================================================*/


#include "usServiceTrackerPrivate.h"
#include "usTrackedService_p.h"
#include "usServiceException.h"
#include "usBundleContext.h"

#include <string>
#include <stdexcept>
#include <limits>

namespace us {

template<class S, class TTT>
ServiceTracker<S,TTT>::~ServiceTracker()
{
  Close();
  delete d;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4355)
#endif

template<class S, class TTT>
ServiceTracker<S,TTT>::ServiceTracker(BundleContext* context,
                                      const ServiceReferenceType& reference,
                                      _ServiceTrackerCustomizer* customizer)
  : d(new _ServiceTrackerPrivate(this, context, reference, customizer))
{
}

template<class S, class TTT>
ServiceTracker<S,TTT>::ServiceTracker(BundleContext* context, const std::string& clazz,
                                      _ServiceTrackerCustomizer* customizer)
  : d(new _ServiceTrackerPrivate(this, context, clazz, customizer))
{
}

template<class S, class TTT>
ServiceTracker<S,TTT>::ServiceTracker(BundleContext* context, const LDAPFilter& filter,
                                      _ServiceTrackerCustomizer* customizer)
  : d(new _ServiceTrackerPrivate(this, context, filter, customizer))
{
}

template<class S, class TTT>
ServiceTracker<S,TTT>::ServiceTracker(BundleContext *context, _ServiceTrackerCustomizer* customizer)
  : d(new _ServiceTrackerPrivate(this, context, us_service_interface_iid<S>(), customizer))
{
  std::string clazz = us_service_interface_iid<S>();
  if (clazz.empty()) throw ServiceException("The service interface class has no US_DECLARE_SERVICE_INTERFACE macro");
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

template<class S, class TTT>
void ServiceTracker<S,TTT>::Open()
{
  _TrackedService* t;
  {
    typename _ServiceTrackerPrivate::Lock l(d);
    if (d->trackedService)
    {
      return;
    }

    US_DEBUG(d->DEBUG_OUTPUT) << "ServiceTracker<S,TTT>::Open: " << d->filter;

    t = new _TrackedService(this, d->customizer);
    {
      typename _TrackedService::Lock l(*t);
      try {
        d->context->AddServiceListener(t, &_TrackedService::ServiceChanged, d->listenerFilter);
        std::vector<ServiceReferenceType> references;
        if (!d->trackClass.empty())
        {
          references = d->GetInitialReferences(d->trackClass, std::string());
        }
        else
        {
          if (d->trackReference.GetBundle() != nullptr)
          {
            references.push_back(d->trackReference);
          }
          else
          { /* user supplied filter */
            references = d->GetInitialReferences(std::string(),
                                                 (d->listenerFilter.empty()) ? d->filter.ToString() : d->listenerFilter);
          }
        }
        /* set tracked with the initial references */
        t->SetInitial(references);
      }
      catch (const std::invalid_argument& e)
      {
        throw std::runtime_error(std::string("unexpected std::invalid_argument exception: ")
            + e.what());
      }
    }
    d->trackedService = t;
  }
  /* Call tracked outside of synchronized region */
  t->TrackInitial(); /* process the initial references */
}

template<class S, class TTT>
void ServiceTracker<S,TTT>::Close()
{
  _TrackedService* outgoing;
  std::vector<ServiceReferenceType> references;
  {
    typename _ServiceTrackerPrivate::Lock l(d);
    outgoing = d->trackedService;
    if (outgoing == nullptr)
    {
      return;
    }
    US_DEBUG(d->DEBUG_OUTPUT) << "ServiceTracker<S,TTT>::close:" << d->filter;
    outgoing->Close();
    references = GetServiceReferences();
    d->trackedService = nullptr;
    try
    {
      d->context->RemoveServiceListener(outgoing, &_TrackedService::ServiceChanged);
    }
    catch (const std::logic_error& /*e*/)
    {
      /* In case the context was stopped. */
    }
  }
  d->Modified(); /* clear the cache */
  {
    typename _TrackedService::Lock l(outgoing);
    outgoing->NotifyAll(); /* wake up any waiters */
  }
  for(auto& ref : references)
  {
    outgoing->Untrack(ref, ServiceEvent());
  }

  if (d->DEBUG_OUTPUT)
  {
    typename _ServiceTrackerPrivate::Lock l(d);
    if ((d->cachedReference.GetBundle() == nullptr) && !TTT::IsValid(d->cachedService))
    {
      US_DEBUG(true) << "ServiceTracker<S,TTT>::close[cached cleared]:"
                       << d->filter;
    }
  }

  delete outgoing;
  d->trackedService = nullptr;
}

template<class S, class TTT>
template<class Rep, class Period>
typename ServiceTracker<S,TTT>::T
ServiceTracker<S,TTT>::WaitForService(const std::chrono::duration<Rep, Period>& rel_time)
{
  T object = GetService();
  while (!TTT::IsValid(object))
  {
    _TrackedService* t = d->Tracked();
    if (t == nullptr)
    { /* if ServiceTracker is not open */
      return TTT::DefaultValue();
    }
    {
      typename _TrackedService::Lock l(t);
      if (t->Size() == 0)
      {
        t->WaitFor(l, rel_time);
      }
    }
    object = GetService();
  }
  return object;
}

template<class S, class TTT>
std::vector<typename ServiceTracker<S,TTT>::ServiceReferenceType>
ServiceTracker<S,TTT>::GetServiceReferences() const
{
  std::vector<ServiceReferenceType> refs;
  _TrackedService* t = d->Tracked();
  if (t == nullptr)
  { /* if ServiceTracker is not open */
    return refs;
  }
  {
    typename _TrackedService::Lock l(t);
    d->GetServiceReferences_unlocked(refs, t);
  }
  return refs;
}

template<class S, class TTT>
typename ServiceTracker<S,TTT>::ServiceReferenceType
ServiceTracker<S,TTT>::GetServiceReference() const
{
  ServiceReferenceType reference;
  {
    typename _ServiceTrackerPrivate::Lock l(d);
    reference = d->cachedReference;
  }
  if (reference.GetBundle() != nullptr)
  {
    US_DEBUG(d->DEBUG_OUTPUT) << "ServiceTracker<S,TTT>::getServiceReference[cached]:"
                         << d->filter;
    return reference;
  }
  US_DEBUG(d->DEBUG_OUTPUT) << "ServiceTracker<S,TTT>::getServiceReference:" << d->filter;
  std::vector<ServiceReferenceType> references = GetServiceReferences();
  std::size_t length = references.size();
  if (length == 0)
  { /* if no service is being tracked */
    throw ServiceException("No service is being tracked");
  }
  typename std::vector<ServiceReferenceType>::const_iterator selectedRef = references.begin();
  if (length > 1)
  { /* if more than one service, select highest ranking */
    std::vector<int> rankings(length);
    int count = 0;
    int maxRanking = (std::numeric_limits<int>::min)();
    typename std::vector<ServiceReferenceType>::const_iterator refIter = references.begin();
    for (std::size_t i = 0; i < length; i++)
    {
      Any rankingAny = refIter->GetProperty(ServiceConstants::SERVICE_RANKING());
      int ranking = 0;
      if (rankingAny.Type() == typeid(int))
      {
        ranking = any_cast<int>(rankingAny);
      }

      rankings[i] = ranking;
      if (ranking > maxRanking)
      {
        selectedRef = refIter;
        maxRanking = ranking;
        count = 1;
      }
      else
      {
        if (ranking == maxRanking)
        {
          count++;
        }
      }
      ++refIter;
    }
    if (count > 1)
    { /* if still more than one service, select lowest id */
      long int minId = (std::numeric_limits<long int>::max)();
      refIter = references.begin();
      for (std::size_t i = 0; i < length; i++)
      {
        if (rankings[i] == maxRanking)
        {
          Any idAny = refIter->GetProperty(ServiceConstants::SERVICE_ID());
          long int id = 0;
          if (idAny.Type() == typeid(long int))
          {
            id = any_cast<long int>(idAny);
          }
          if (id < minId)
          {
            selectedRef = refIter;
            minId = id;
          }
        }
        ++refIter;
      }
    }
  }

  {
    typename _ServiceTrackerPrivate::Lock l(d);
    d->cachedReference = *selectedRef;
    return d->cachedReference;
  }
}

template<class S, class TTT>
typename ServiceTracker<S,TTT>::T
ServiceTracker<S,TTT>::GetService(const ServiceReferenceType& reference) const
{
  _TrackedService* t = d->Tracked();
  if (t == nullptr)
  { /* if ServiceTracker is not open */
    return TTT::DefaultValue();
  }
  {
    typename _TrackedService::Lock l(t);
    return t->GetCustomizedObject(reference);
  }
}

template<class S, class TTT>
std::vector<typename ServiceTracker<S,TTT>::T> ServiceTracker<S,TTT>::GetServices() const
{
  std::vector<T> services;
  _TrackedService* t = d->Tracked();
  if (t == nullptr)
  { /* if ServiceTracker is not open */
    return services;
  }
  {
    typename _TrackedService::Lock l(t);
    std::vector<ServiceReferenceType> references;
    d->GetServiceReferences_unlocked(references, t);
    for(typename std::vector<ServiceReferenceType>::const_iterator ref = references.begin();
        ref != references.end(); ++ref)
    {
      services.push_back(t->GetCustomizedObject(*ref));
    }
  }
  return services;
}

template<class S, class TTT>
typename ServiceTracker<S,TTT>::T
ServiceTracker<S,TTT>::GetService() const
{
  {
    typename _ServiceTrackerPrivate::Lock l(d);
    const T& service = d->cachedService;
    if (TTT::IsValid(service))
    {
      US_DEBUG(d->DEBUG_OUTPUT) << "ServiceTracker<S,TTT>::getService[cached]:"
                                << d->filter;
      return service;
    }
  }
  US_DEBUG(d->DEBUG_OUTPUT) << "ServiceTracker<S,TTT>::getService:" << d->filter;

  try
  {
    ServiceReferenceType reference = GetServiceReference();
    if (reference.GetBundle() == nullptr)
    {
      return TTT::DefaultValue();
    }
    {
      typename _ServiceTrackerPrivate::Lock l(d);
      return d->cachedService = GetService(reference);
    }
  }
  catch (const ServiceException&)
  {
    return TTT::DefaultValue();
  }
}

template<class S, class TTT>
void ServiceTracker<S,TTT>::Remove(const ServiceReferenceType& reference)
{
  _TrackedService* t = d->Tracked();
  if (t == nullptr)
  { /* if ServiceTracker is not open */
    return;
  }
  t->Untrack(reference, ServiceEvent());
}

template<class S, class TTT>
int ServiceTracker<S,TTT>::Size() const
{
  _TrackedService* t = d->Tracked();
  if (t == nullptr)
  { /* if ServiceTracker is not open */
    return 0;
  }
  {
    typename _TrackedService::Lock l(t);
    return static_cast<int>(t->Size());
  }
}

template<class S, class TTT>
int ServiceTracker<S,TTT>::GetTrackingCount() const
{
  _TrackedService* t = d->Tracked();
  if (t == nullptr)
  { /* if ServiceTracker is not open */
    return -1;
  }
  {
    typename _TrackedService::Lock l(t);
    return t->GetTrackingCount();
  }
}

template<class S, class TTT>
void ServiceTracker<S,TTT>::GetTracked(TrackingMap& map) const
{
  _TrackedService* t = d->Tracked();
  if (t == nullptr)
  { /* if ServiceTracker is not open */
    return;
  }
  {
    typename _TrackedService::Lock l(t);
    t->CopyEntries(map);
  }
}

template<class S, class TTT>
bool ServiceTracker<S,TTT>::IsEmpty() const
{
  _TrackedService* t = d->Tracked();
  if (t == nullptr)
  { /* if ServiceTracker is not open */
    return true;
  }
  {
    typename _TrackedService::Lock l(t);
    return t->IsEmpty();
  }
}

template<class S, class TTT>
typename ServiceTracker<S,TTT>::T
ServiceTracker<S,TTT>::AddingService(const ServiceReferenceType& reference)
{
  return TTT::ConvertToTrackedType(d->context->GetService(reference));
}

template<class S, class TTT>
void ServiceTracker<S,TTT>::ModifiedService(const ServiceReferenceType& /*reference*/, T /*service*/)
{
  /* do nothing */
}

template<class S, class TTT>
void ServiceTracker<S,TTT>::RemovedService(const ServiceReferenceType& reference, T /*service*/)
{
  d->context->UngetService(reference);
}

}
