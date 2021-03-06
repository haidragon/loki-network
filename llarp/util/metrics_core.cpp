#include <util/metrics_core.hpp>

#include <iostream>

namespace llarp
{
  namespace metrics
  {
    Record
    IntCollector::loadAndClear()
    {
      size_t count;
      uint64_t total;
      int min;
      int max;

      {
        absl::WriterMutexLock l(&m_mutex);

        count = m_count;
        total = m_total;
        min   = m_min;
        max   = m_max;

        m_count = 0;
        m_total = 0;
        m_min   = DEFAULT_MIN;
        m_max   = DEFAULT_MAX;
      }

      return {m_id, count, static_cast< double >(total),
              (min == DEFAULT_MIN) ? Record::DEFAULT_MIN
                                   : static_cast< double >(min),
              (max == DEFAULT_MAX) ? Record::DEFAULT_MAX
                                   : static_cast< double >(max)};
    }

    Record
    IntCollector::load()
    {
      size_t count;
      int64_t total;
      int min;
      int max;

      {
        absl::ReaderMutexLock l(&m_mutex);

        count = m_count;
        total = m_total;
        min   = m_min;
        max   = m_max;
      }

      return {m_id, count, static_cast< double >(total),
              (min == DEFAULT_MIN) ? Record::DEFAULT_MIN : min,
              (max == DEFAULT_MAX) ? Record::DEFAULT_MAX : max};
    }

    std::tuple< Id, bool >
    Registry::insert(const char *category, const char *name)
    {
      // avoid life time issues, putting strings in the stringmem set
      const char *cStr = m_stringmem.emplace(category).first->c_str();
      const char *nStr = m_stringmem.emplace(name).first->c_str();

      NamedCategory namedCategory(cStr, nStr);
      const auto it = m_metrics.find(namedCategory);

      if(it != m_metrics.end())
      {
        return std::make_tuple(Id(it->second.get()), false);
      }

      auto cIt = m_categories.find(cStr);
      if(cIt == m_categories.end())
      {
        auto ptr = std::make_shared< Category >(cStr, m_defaultEnabled);
        cIt      = m_categories.emplace(cStr, ptr).first;
      }

      const auto mPtr =
          std::make_shared< Description >(cIt->second.get(), nStr);

      m_metrics.emplace(namedCategory, mPtr);
      return {Id(mPtr.get()), true};
    }

    Id
    Registry::add(const char *category, const char *name)
    {
      absl::WriterMutexLock l(&m_mutex);
      auto result = insert(category, name);
      return std::get< 1 >(result) ? std::get< 0 >(result) : Id();
    }

    Id
    Registry::get(const char *category, const char *name)
    {
      Id result = findId(category, name);
      if(result)
      {
        return result;
      }

      absl::WriterMutexLock l(&m_mutex);
      return std::get< 0 >(insert(category, name));
    }

    const Category *
    Registry::add(const char *category)
    {
      absl::WriterMutexLock l(&m_mutex);

      const char *cStr = m_stringmem.emplace(category).first->c_str();
      auto it          = m_categories.find(cStr);
      if(it == m_categories.end())
      {
        auto ptr = std::make_shared< Category >(cStr, m_defaultEnabled);
        it       = m_categories.emplace(cStr, ptr).first;
        return it->second.get();
      }
      return nullptr;
    }

    const Category *
    Registry::get(const char *category)
    {
      const Category *cPtr = findCategory(category);
      if(cPtr)
      {
        return cPtr;
      }

      absl::WriterMutexLock l(&m_mutex);
      const char *cStr = m_stringmem.emplace(category).first->c_str();
      auto it          = m_categories.find(cStr);
      if(it == m_categories.end())
      {
        auto ptr = std::make_shared< Category >(cStr, m_defaultEnabled);
        it       = m_categories.emplace(cStr, ptr).first;
      }
      return it->second.get();
    }

    void
    Registry::enable(const Category *category, bool value)
    {
      absl::WriterMutexLock l(&m_mutex);
      const_cast< Category * >(category)->enabled(value);
    }

    void
    Registry::enableAll(bool value)
    {
      absl::WriterMutexLock l(&m_mutex);

      if(value == m_defaultEnabled)
      {
        return;
      }

      m_defaultEnabled = value;

      std::for_each(m_categories.begin(), m_categories.end(),
                    [&](auto &x) { x.second->enabled(value); });
    }

    void
    Registry::registerContainer(const Category *category,
                                CategoryContainer &container)
    {
      absl::WriterMutexLock l(&m_mutex);
      if(container.m_category == nullptr)
      {
        const_cast< Category * >(category)->registerContainer(&container);
      }
    }

    void
    Registry::publicationType(const Id &id, Publication::Type type)
    {
      const_cast< Description * >(id.description())->type(type);
    }

    void
    Registry::setFormat(const Id &id, const Format &format)
    {
      Description *description = const_cast< Description * >(id.description());

      absl::WriterMutexLock l(&m_mutex);

      auto fmtPtr = std::make_shared< Format >(format);

      for(byte_t i = 0; i < Publication::MaxSize; ++i)
      {
        auto type              = static_cast< Publication::Type >(i);
        const FormatSpec *spec = format.specFor(type);
        if(spec != nullptr)
        {
          const char *fmt = m_stringmem.emplace(spec->m_format).first->c_str();
          fmtPtr->setSpec(type, FormatSpec(spec->m_scale, fmt));
        }
      }

      description->format(fmtPtr);
    }

    const Category *
    Registry::findCategory(const char *category) const
    {
      absl::ReaderMutexLock l(&m_mutex);
      auto it = m_categories.find(category);
      return it == m_categories.end() ? nullptr : it->second.get();
    }

    Id
    Registry::findId(const char *category, const char *name) const
    {
      absl::ReaderMutexLock l(&m_mutex);
      auto it = m_metrics.find(std::make_tuple(category, name));
      return it == m_metrics.end() ? Id() : Id(it->second.get());
    }

    std::vector< const Category * >
    Registry::getAll() const
    {
      absl::ReaderMutexLock l(&m_mutex);

      std::vector< const Category * > result;
      result.reserve(m_categories.size());

      std::transform(m_categories.begin(), m_categories.end(),
                     std::back_inserter(result),
                     [](const auto &x) { return x.second.get(); });

      return result;
    }

    MetricCollectors &
    CollectorRepo::getCollectors(const Id &id)
    {
      auto it = m_collectors.find(id);

      if(it == m_collectors.end())
      {
        assert(id.valid());

        const Category *cat = id.category();

        auto ptr  = std::make_shared< MetricCollectors >(id);
        auto &vec = m_categories[cat];
        vec.reserve(vec.size() + 1);

        it = m_collectors.emplace(id, ptr).first;
        vec.push_back(ptr.get());
      }

      return *it->second.get();
    }

    std::vector< Record >
    CollectorRepo::collectAndClear(const Category *category)
    {
      absl::WriterMutexLock l(&m_mutex);

      std::vector< Record > result;

      auto it = m_categories.find(category);

      if(it != m_categories.end())
      {
        auto &collectors = it->second;
        result.reserve(collectors.size());

        std::transform(
            collectors.begin(), collectors.end(), std::back_inserter(result),
            [](MetricCollectors *c) { return c->combineAndClear(); });
      }

      return result;
    }

    std::vector< Record >
    CollectorRepo::collect(const Category *category)
    {
      absl::WriterMutexLock l(&m_mutex);

      std::vector< Record > result;

      auto it = m_categories.find(category);

      if(it != m_categories.end())
      {
        auto &collectors = it->second;
        result.reserve(collectors.size());

        std::transform(collectors.begin(), collectors.end(),
                       std::back_inserter(result),
                       [](MetricCollectors *c) { return c->combine(); });
      }

      return result;
    }

    DoubleCollector *
    CollectorRepo::defaultDoubleCollector(const Id &id)
    {
      {
        absl::ReaderMutexLock l(&m_mutex);
        auto it = m_collectors.find(id);
        if(it != m_collectors.end())
        {
          return it->second->doubleCollectors().defaultCollector();
        }
      }

      {
        absl::WriterMutexLock l(&m_mutex);
        return getCollectors(id).doubleCollectors().defaultCollector();
      }
    }

    IntCollector *
    CollectorRepo::defaultIntCollector(const Id &id)
    {
      {
        absl::ReaderMutexLock l(&m_mutex);
        auto it = m_collectors.find(id);
        if(it != m_collectors.end())
        {
          return it->second->intCollectors().defaultCollector();
        }
      }

      {
        absl::WriterMutexLock l(&m_mutex);
        return getCollectors(id).intCollectors().defaultCollector();
      }
    }

    std::pair< std::vector< std::shared_ptr< DoubleCollector > >,
               std::vector< std::shared_ptr< IntCollector > > >
    CollectorRepo::allCollectors(const Id &id)
    {
      absl::ReaderMutexLock l(&m_mutex);

      auto it = m_collectors.find(id);

      if(it == m_collectors.end())
      {
        return {};
      }

      return {it->second->doubleCollectors().collectors(),
              it->second->intCollectors().collectors()};
    }

    struct PublisherHelper
    {
      using SampleCache = std::map< std::shared_ptr< Publisher >, Sample >;

      static void
      updateSampleCache(SampleCache &cache,
                        const std::shared_ptr< Publisher > &publisher,
                        const SampleGroup &sampleGroup,
                        const absl::Time &timeStamp)
      {
        SampleCache::iterator it = cache.find(publisher);
        if(it == cache.end())
        {
          Sample newSample;
          newSample.sampleTime(timeStamp);
          it = cache.emplace(publisher, newSample).first;
        }
        it->second.pushGroup(sampleGroup);
      }

      static std::pair< std::vector< Record >, absl::Duration >
      collect(Manager &manager, const Category *category,
              const absl::Duration &now, bool clear)
          EXCLUSIVE_LOCKS_REQUIRED(manager.m_mutex)
      {
        using Callback         = Manager::RecordCallback;
        using CallbackVector   = std::vector< const Callback * >;
        using RegistryIterator = CallbackRegistry::iterator;
        CallbackVector callbacks;

        RegistryIterator begin = manager.m_callbacks.lowerBound(category);
        RegistryIterator end   = manager.m_callbacks.upperBound(category);

        std::vector< Record > result;
        std::for_each(begin, end, [&](const auto &x) {
          std::vector< Record > tmp = (x.second)(clear);
          result.insert(result.end(), tmp.begin(), tmp.end());
        });

        // Collect records from the repo.
        if(clear)
        {
          std::vector< Record > tmp = manager.m_repo.collectAndClear(category);
          result.insert(result.end(), tmp.begin(), tmp.end());
        }
        else
        {
          std::vector< Record > tmp = manager.m_repo.collect(category);
          result.insert(result.end(), tmp.begin(), tmp.end());
        }

        // Get the time since last reset, and clear if needed.
        Manager::ResetTimes::iterator it = manager.m_resetTimes.find(category);
        if(it == manager.m_resetTimes.end())
        {
          if(clear)
          {
            manager.m_resetTimes.emplace(category, now);
          }
          return {result, now - manager.m_createTime};
        }
        else
        {
          auto tmp = now - it->second;
          if(clear)
          {
            it->second = now;
          }
          return {result, tmp};
        }
      }

      template < typename CategoryIterator >
      static void
      publish(Manager &manager, const CategoryIterator &categoriesBegin,
              const CategoryIterator &categoriesEnd, bool clear)
      {
        if(categoriesBegin == categoriesEnd)
        {
          return;
        }
        using RecordBuffer =
            std::vector< std::shared_ptr< std::vector< Record > > >;

        RecordBuffer recordBuffer;

        SampleCache sampleCache;

        absl::Time timeStamp = absl::Now();
        absl::Duration now   = absl::Now() - absl::UnixEpoch();
        {
          // 1.
          absl::WriterMutexLock publishGuard(&manager.m_publishLock);
          // 2.
          absl::WriterMutexLock propertiesGuard(&manager.m_mutex);

          // Build the 'sampleCache' by iterating over the categories and
          // collecting records for those categories.
          for(CategoryIterator catIt = categoriesBegin; catIt != categoriesEnd;
              ++catIt)
          {
            if(!(*catIt)->enabled())
            {
              continue;
            }
            // Collect the metrics.
            auto result = collect(manager, *catIt, now, clear);

            // If their are no collected records then this category can be
            // ignored.
            if(result.first.empty())
            {
              continue;
            }

            if(result.second == absl::Duration())
            {
              std::cerr << "Invalid elapsed time interval of 0 for "
                           "published metrics.";
              result.second += absl::Nanoseconds(1);
            }

            // Append the collected records to the buffer of records.
            auto records =
                std::make_shared< std::vector< Record > >(result.first);
            recordBuffer.push_back(records);
            SampleGroup sampleGroup(absl::Span< Record >(*records),
                                    result.second);

            std::for_each(
                manager.m_publishers.globalBegin(),
                manager.m_publishers.globalEnd(), [&](const auto &ptr) {
                  updateSampleCache(sampleCache, ptr, sampleGroup, timeStamp);
                });

            std::for_each(manager.m_publishers.lowerBound(*catIt),
                          manager.m_publishers.upperBound(*catIt),
                          [&](const auto &val) {
                            updateSampleCache(sampleCache, val.second,
                                              sampleGroup, timeStamp);
                          });
          }
        }

        for(auto &entry : sampleCache)
        {
          Publisher *publisher = entry.first.get();
          Sample &sample       = entry.second;

          publisher->publish(sample);
        }
      }
    };

    Sample
    Manager::collectSample(std::vector< Record > &records,
                           absl::Span< const Category * > categories,
                           bool clear)
    {
      absl::Time timeStamp = absl::Now();
      absl::Duration now   = timeStamp - absl::UnixEpoch();

      Sample sample;
      sample.sampleTime(timeStamp);

      // Use a tuple to hold 'references' to the collected records
      using SampleDescription = std::tuple< size_t, size_t, absl::Duration >;
      std::vector< SampleDescription > samples;
      samples.reserve(categories.size());

      // 1
      absl::WriterMutexLock publishGuard(&m_publishLock);
      // 2
      absl::WriterMutexLock propertiesGuard(&m_mutex);

      for(const Category *const category : categories)
      {
        if(!category->enabled())
        {
          continue;
        }

        size_t beginIndex = records.size();

        // Collect the metrics.
        std::vector< Record > catRecords;
        absl::Duration elapsedTime;
        std::tie(catRecords, elapsedTime) =
            PublisherHelper::collect(*this, category, now, clear);
        records.insert(records.end(), catRecords.begin(), catRecords.end());

        size_t size = records.size() - beginIndex;

        // If there are no collected records then this category can be ignored.
        if(size != 0)
        {
          samples.emplace_back(beginIndex, size, elapsedTime);
        }
      }

      // Now that we have all the records, we can build our sample
      for(const SampleDescription &s : samples)
      {
        sample.pushGroup(&records[std::get< 0 >(s)], std::get< 1 >(s),
                         std::get< 2 >(s));
      }

      return sample;
    }

    void
    Manager::publish(absl::Span< const Category * > categories, bool clear)
    {
      PublisherHelper::publish(*this, categories.begin(), categories.end(),
                               clear);
    }

    void
    Manager::publish(const std::set< const Category * > &categories, bool clear)
    {
      PublisherHelper::publish(*this, categories.begin(), categories.end(),
                               clear);
    }

    Manager *DefaultManager::m_manager = nullptr;

    struct PublisherSchedulerData
    {
      util::Mutex m_mutex;
      thread::Scheduler::Handle m_handle;
      std::set< const Category * > m_categories;

      bool m_default;
      std::set< const Category * > m_nonDefaultCategories;

      PublisherSchedulerData()
          : m_handle(thread::Scheduler::INVALID_HANDLE), m_default(false)
      {
      }
    };

    // Reverts a publisher scheduler back to its default state
    class PublisherSchedulerGuard
    {
      PublisherScheduler *m_scheduler;

     public:
      PublisherSchedulerGuard(PublisherScheduler *scheduler)
          : m_scheduler(scheduler)
      {
      }

      ~PublisherSchedulerGuard()
      {
        if(m_scheduler != nullptr)
        {
          for(auto &repeat : m_scheduler->m_repeaters)
          {
            if(repeat.second->m_handle != thread::Scheduler::INVALID_HANDLE)
            {
              m_scheduler->m_scheduler.cancelRepeat(repeat.second->m_handle);
            }
          }

          m_scheduler->m_defaultInterval = absl::Duration();
          m_scheduler->m_repeaters.clear();
          m_scheduler->m_categories.clear();
        }
      }

      void
      release()
      {
        m_scheduler = nullptr;
      }
    };

    void
    PublisherScheduler::publish(
        const std::shared_ptr< PublisherSchedulerData > &data) const
    {
      util::Lock l(&data->m_mutex);
      if(data->m_default)
      {
        m_manager->publishAllExcluding(data->m_nonDefaultCategories);
      }
      else if(!data->m_categories.empty())
      {
        m_manager->publish(data->m_categories);
      }
    }

    void
    PublisherScheduler::cancel(Categories::iterator it)
    {
      assert(it != m_categories.end());
      auto repeatIt = m_repeaters.find(it->second);
      assert(repeatIt != m_repeaters.end());

      const Category *category = it->first;
      m_categories.erase(it);
      auto data = repeatIt->second;

      {
        util::Lock l(&data->m_mutex);
        assert(data->m_categories.find(category) != data->m_categories.end());
        data->m_categories.erase(category);
      }

      if(!data->m_default)
      {
        if(data->m_categories.empty())
        {
          m_scheduler.cancelRepeat(data->m_handle);
          m_repeaters.erase(repeatIt);
        }

        if(m_defaultInterval != absl::Duration())
        {
          auto defaultIntervalIt = m_repeaters.find(m_defaultInterval);
          assert(defaultIntervalIt != m_repeaters.end());

          auto &defaultRepeater = defaultIntervalIt->second;
          util::Lock l(&defaultRepeater->m_mutex);
          defaultRepeater->m_nonDefaultCategories.erase(category);
        }
      }
    }

    bool
    PublisherScheduler::cancelDefault()
    {
      if(m_defaultInterval == absl::Duration())
      {
        return false;
      }

      absl::Duration interval = m_defaultInterval;
      m_defaultInterval       = absl::Duration();

      auto repeatIt = m_repeaters.find(interval);
      assert(repeatIt != m_repeaters.end());
      auto data = repeatIt->second;

      if(data->m_categories.empty())
      {
        assert(data->m_handle != thread::Scheduler::INVALID_HANDLE);
        m_scheduler.cancelRepeat(data->m_handle);
        m_repeaters.erase(repeatIt);
      }
      else
      {
        util::Lock l(&data->m_mutex);
        data->m_default = false;
        data->m_nonDefaultCategories.clear();
      }
      return true;
    }

    void
    PublisherScheduler::schedule(const Category *category,
                                 absl::Duration interval)
    {
      assert(absl::Seconds(0) < interval);

      util::Lock l(&m_mutex);

      auto catIt = m_categories.find(category);
      if(catIt != m_categories.end())
      {
        if(catIt->second == interval)
        {
          return;
        }
        cancel(catIt);
      }

      // Make a guard, so if something throws, the scheduler is reset to a
      // somewhat "sane" state (no metrics).
      PublisherSchedulerGuard guard(this);

      m_categories.emplace(category, interval);
      auto repeatIt = m_repeaters.find(interval);
      std::shared_ptr< PublisherSchedulerData > data;

      // Create a new 'ClockData' object if one does not exist for the
      // 'interval', otherwise update the existing 'data'.
      if(repeatIt == m_repeaters.end())
      {
        data = std::make_shared< PublisherSchedulerData >();
        data->m_categories.insert(category);
        m_repeaters.emplace(interval, data);
        util::Lock lock(&data->m_mutex);
        data->m_handle = m_scheduler.scheduleRepeat(
            interval, std::bind(&PublisherScheduler::publish, this, data));
      }
      else
      {
        data = repeatIt->second;
        util::Lock lock(&data->m_mutex);
        data->m_categories.insert(category);
      }

      // If this isn't being added to the default schedule, then add to the set
      // of non-default categories in the default schedule.

      if(!data->m_default && m_defaultInterval != absl::Duration())
      {
        auto defaultIntervalIt = m_repeaters.find(m_defaultInterval);
        assert(defaultIntervalIt != m_repeaters.end());

        auto &defaultInterval = defaultIntervalIt->second;
        util::Lock lock(&defaultInterval->m_mutex);
        defaultInterval->m_nonDefaultCategories.insert(category);
      }

      guard.release();
    }

    void
    PublisherScheduler::setDefault(absl::Duration interval)
    {
      assert(absl::Seconds(0) < interval);
      util::Lock l(&m_mutex);

      // If its already this interval, return early.
      if(interval == m_defaultInterval)
      {
        return;
      }

      cancelDefault();
      m_defaultInterval = interval;

      // Make a guard, so if something throws, the scheduler is reset to a
      // somewhat "sane" state (no metrics).
      PublisherSchedulerGuard guard(this);

      std::shared_ptr< PublisherSchedulerData > data;
      auto repeatIt = m_repeaters.find(interval);
      if(repeatIt == m_repeaters.end())
      {
        data = std::make_shared< PublisherSchedulerData >();
        m_repeaters.emplace(interval, data);
      }
      else
      {
        data = repeatIt->second;
      }

      util::Lock lock(&data->m_mutex);
      data->m_default = true;

      Categories::iterator cIt = m_categories.begin();
      for(; cIt != m_categories.end(); ++cIt)
      {
        if(cIt->second != interval)
        {
          data->m_nonDefaultCategories.insert(cIt->first);
        }
      }

      if(data->m_handle == thread::Scheduler::INVALID_HANDLE)
      {
        data->m_handle = m_scheduler.scheduleRepeat(
            interval, std::bind(&PublisherScheduler::publish, this, data));
      }

      guard.release();
    }

    bool
    PublisherScheduler::cancel(const Category *category)
    {
      util::Lock l(&m_mutex);

      Categories::iterator it = m_categories.find(category);
      if(it == m_categories.end())
      {
        // This category has no specific schedule.
        return false;
      }
      cancel(it);

      return true;
    }

    bool
    PublisherScheduler::clearDefault()
    {
      util::Lock l(&m_mutex);
      return cancelDefault();
    }

    void
    PublisherScheduler::cancelAll()
    {
      util::Lock l(&m_mutex);
      for(auto &repeat : m_repeaters)
      {
        m_scheduler.cancelRepeat(repeat.second->m_handle, true);
      }

      m_defaultInterval = absl::Duration();
      m_repeaters.clear();
      m_categories.clear();
    }

    absl::optional< absl::Duration >
    PublisherScheduler::find(const Category *category) const
    {
      util::Lock l(&m_mutex);
      auto it = m_categories.find(category);

      if(it == m_categories.end())
      {
        return {};
      }
      else
      {
        return it->second;
      }
    }

    absl::optional< absl::Duration >
    PublisherScheduler::getDefault() const
    {
      util::Lock l(&m_mutex);

      if(m_defaultInterval == absl::Duration())
      {
        return {};
      }
      else
      {
        return m_defaultInterval;
      }
    }

    std::vector< std::pair< const Category *, absl::Duration > >
    PublisherScheduler::getAll() const
    {
      util::Lock l(&m_mutex);
      std::vector< std::pair< const Category *, absl::Duration > > result;
      result.reserve(m_categories.size());
      std::copy(m_categories.begin(), m_categories.end(),
                std::back_inserter(result));
      return result;
    }

  }  // namespace metrics
}  // namespace llarp
