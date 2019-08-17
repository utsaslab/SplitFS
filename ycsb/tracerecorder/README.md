<!--
Copyright (c) 2015 YCSB contributors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you
may not use this file except in compliance with the License. You
may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing
permissions and limitations under the License. See accompanying
LICENSE file.
-->

# TraceRecorder

TraceRecorder is used to capture a trace of YCSB workloads so that the trace can then be replayed over any key-value store. Set the file name to write the trace to the environmental variable `recorder.file`.

## Sample Usage

    ./bin/ycsb load tracerecorder -p recorder.file=loada_5M.txt -p recordcount=5000000 -P workloads/workloada
    ./bin/ycsb run tracerecorder -p recorder.file=runa_1M.txt -p recordcount=5000000 -p operationcount=1000000 -P workloads/workloada
