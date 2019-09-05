#include <mesos/module/resource_estimator.hpp>

#include <mesos/slave/resource_estimator.hpp>

#include <process/defer.hpp>
#include <process/dispatch.hpp>
#include <process/id.hpp>
#include <process/owned.hpp>
#include <process/process.hpp>

#include <stout/lambda.hpp>
#include <stout/os.hpp>

using namespace mesos;
using namespace process;

using mesos::modules::Module;

using mesos::slave::ResourceEstimator;


class DynamicResourceEstimatorProcess
  : public Process<DynamicResourceEstimatorProcess>
{
public:
  DynamicResourceEstimatorProcess(
      const lambda::function<Future<ResourceUsage>()>& _usage,
      const Resources& _totalRevocable,
      const double& _upperShreshold,
      const double& _lowerShreshol)
    : ProcessBase(process::ID::generate("fixed-resource-estimator")),
      usage(_usage),
      totalRevocable(_totalRevocable),
      loadUpperLimit(_upperShreshold),
      loadLowerLimit(_lowerShreshol) {
    foreach (Resource resource, _totalRevocable) {
      if(resource.name() == "cpus") {
        revocableLimit = resource.scalar().value();
      }
    }
  }

  Future<Resources> oversubscribable()
  {
    return usage().then(defer(self(), &Self::_oversubscribable, lambda::_1));
  }

  Future<Resources> _oversubscribable(const ResourceUsage& usage)
  {
    Resources allocatedRevocable;
    foreach (const ResourceUsage::Executor& executor, usage.executors()) {
      allocatedRevocable += Resources(executor.allocated()).revocable();
    }

    auto unallocated = [](const Resources& resources) {
      Resources result = resources;
      result.unallocate();
      return result;
    };

    double factor = 0;

    Try<os::Load> load = os::loadavg(); 
    // We only care about the last 1min's load.
    if (load.isSome()) {
      if (load->one >= loadUpperLimit) {
        factor = 1;
      } else if (load->one > loadLowerLimit) {
        // linear
        // factor = (load->one-loadLowerLimit)/(loadUpperLimit-loadLowerLimit);

        // parabolic
        factor = (load->one-loadLowerLimit)/(loadUpperLimit-loadLowerLimit);
        factor *= factor;
      }
    }

    Resource loss = Resources::parse("cpus", std::to_string(int(factor*revocableLimit)), "*").get();
    loss.mutable_revocable();

    return totalRevocable - loss - unallocated(allocatedRevocable);
  }

protected:
  const lambda::function<Future<ResourceUsage>()> usage;
  const Resources totalRevocable;
private:
  double loadUpperLimit; 
  double loadLowerLimit; 
  double revocableLimit; 
};


class DynamicResourceEstimator : public ResourceEstimator
{
public:
  DynamicResourceEstimator(const Resources& _totalRevocable, const double& upperLimit, 
  const double& lowerLimit)
  : loadUpperLimit(upperLimit),
    loadLowerLimit(lowerLimit) {
    // Mark all resources as revocable.
    foreach (Resource resource, _totalRevocable) {
      resource.mutable_revocable();
      totalRevocable += resource;
    }
  }

  ~DynamicResourceEstimator() override
  {
    if (process.get() != nullptr) {
      terminate(process.get());
      wait(process.get());
    }
  }

  Try<Nothing> initialize(
      const lambda::function<Future<ResourceUsage>()>& usage) override
  {
    if (process.get() != nullptr) {
      return Error("Dynamic resource estimator has already been initialized");
    }

    process.reset(new DynamicResourceEstimatorProcess(usage, totalRevocable, loadUpperLimit, loadLowerLimit));
    spawn(process.get());

    return Nothing();
  }

  Future<Resources> oversubscribable() override
  {
    if (process.get() == nullptr) {
      return Failure("Dynamic resource estimator is not initialized");
    }

    return dispatch(
        process.get(),
        &DynamicResourceEstimatorProcess::oversubscribable);
  }

private:
  Resources totalRevocable;
  double loadUpperLimit;
  double loadLowerLimit;
  Owned<DynamicResourceEstimatorProcess> process;
};


static bool compatible()
{
  return true;
}


static ResourceEstimator* create(const Parameters& parameters)
{
  // Obtain the *fixed* resources from parameters.
  Option<Resources> resources;
  double loadUpperLimit = 50;
  double loadLowerLimit = 10;
  foreach (const Parameter& parameter, parameters.parameter()) {
    if (parameter.key() == "resources") {
      Try<Resources> _resources = Resources::parse(parameter.value());
      if (_resources.isError()) {
        return nullptr;
      }

      resources = _resources.get();
    } else if (parameter.key() == "load_upper_limit") {
      loadUpperLimit = std::stof(parameter.value());
    } else if (parameter.key() == "load_lower_limit") {
      loadLowerLimit = std::stof(parameter.value());
    }
  }

  if (resources.isNone()) {
    return nullptr;
  }

  return new DynamicResourceEstimator(resources.get(), loadUpperLimit, loadLowerLimit);
}


Module<ResourceEstimator> com_bytedance_DynamicResourceEstimator(
    MESOS_MODULE_API_VERSION,
    MESOS_VERSION,
    "Bytedance",
    "longfei@bytedance.com",
    "Dynamic Resource Estimator Module.",
    compatible,
    create);

