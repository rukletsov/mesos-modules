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

#include <string>
#include <fstream>

#include <mesos/hook.hpp>
#include <mesos/module/hook.hpp>

#include <stout/foreach.hpp>
#include <stout/none.hpp>
#include <stout/result.hpp>
#include <stout/try.hpp>

using namespace mesos;


class TestHook : public Hook
{
public:
  TestHook(const std::string& filename) : out(filename)
  { }

  virtual Result<Labels> slaveRunTaskLabelDecorator(
      const TaskInfo& taskInfo,
      const FrameworkInfo& frameworkInfo,
      const SlaveInfo& slaveInfo)
  {
    out << "Task \"" << taskInfo.task_id().value()\
        << "\" starts on the slave \""
        << slaveInfo.id().value() << "\" [" << slaveInfo.hostname() << ":"
        << slaveInfo.port() << "]" << std::endl;
    out.flush();
    return None();
  }

private:
  std::ofstream out;
};


static Hook* createHook(const Parameters& parameters)
{
  std::string log_filename;
  foreach (const mesos::Parameter& parameter, parameters.parameter()) {
    if (parameter.has_key() &&
        parameter.has_value() &&
        parameter.key() == "log") {
      log_filename = parameter.value();
    }
  }

  if (log_filename.empty()) {
    return NULL;
  }

  return new TestHook(log_filename);
}


// Declares a Hook module named 'TestTaskHook'.
mesos::modules::Module<Hook> TestTaskHook(
    MESOS_MODULE_API_VERSION,
    MESOS_VERSION,
    "Apache Mesos",
    "alex@mesosphere.io",
    "Test Hook module.",
    NULL,
    createHook);
