//
// Created by Marius Meyer on 04.12.19.
//

/*
Copyright (c) 2019 Marius Meyer

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "stream_benchmark.hpp"

/* C++ standard library headers */
#include <memory>
#include <random>

/* Project's headers */
#include "execution.hpp"
#include "parameters.h"

stream::StreamProgramSettings::StreamProgramSettings(cxxopts::ParseResult &results) : hpcc_base::BaseSettings(results),
    streamArraySize(results["s"].as<uint>()),
    kernelReplications(results["r"].as<uint>()),
    useSingleKernel(static_cast<bool>(results.count("single-kernel"))) {

}

std::map<std::string, std::string>
stream::StreamProgramSettings::getSettingsMap() {
        auto map = hpcc_base::BaseSettings::getSettingsMap();
        map["Data Type"] = STR(HOST_DATA_TYPE);
        std::stringstream ss;
        ss << streamArraySize << " (" << static_cast<double>(streamArraySize * sizeof(HOST_DATA_TYPE)) << " Byte )";
        map["Array Size"] = ss.str();
        map["Kernel Replications"] = std::to_string(kernelReplications);
        map["Kernel Type"] = (useSingleKernel ? "Single" : "Separate");
        return map;
}

stream::StreamBenchmark::StreamBenchmark(int argc, char* argv[]) {
    setupBenchmark(argc, argv);
}

stream::StreamBenchmark::StreamBenchmark() {}

void
stream::StreamBenchmark::addAdditionalParseOptions(cxxopts::Options &options) {
        options.add_options()
            ("s", "Size of the data arrays",
             cxxopts::value<uint>()->default_value(std::to_string(DEFAULT_ARRAY_LENGTH)))
            ("r", "Number of kernel replications used",
             cxxopts::value<uint>()->default_value(std::to_string(NUM_KERNEL_REPLICATIONS)))
            ("single-kernel", "Use the single kernel implementation");
}

std::unique_ptr<stream::StreamExecutionTimings>
stream::StreamBenchmark::executeKernel(StreamData &data) {
    return bm_execution::calculate(*executionSettings,
              data.A,
              data.B,
              data.C);
}

void
stream::StreamBenchmark::printResults(const stream::StreamExecutionTimings &output) {

    std::cout << std::setw(ENTRY_SPACE) << "Function";
    std::cout << std::setw(ENTRY_SPACE) << "Best Rate MB/s";
    std::cout << std::setw(ENTRY_SPACE) << "Avg time s";
    std::cout << std::setw(ENTRY_SPACE) << "Min time" ;
    std::cout << std::setw(ENTRY_SPACE) << "Max time" << std::endl;

    for (auto v : output.timings) {
        double minTime = *min_element(v.second.begin(), v.second.end());
        double avgTime = accumulate(v.second.begin(), v.second.end(), 0.0)
                         / v.second.size();
        double maxTime = *max_element(v.second.begin(), v.second.end());

        std::cout << std::setw(ENTRY_SPACE) << v.first;
        std::cout << std::setw(ENTRY_SPACE)
        << (static_cast<double>(sizeof(HOST_DATA_TYPE)) * output.arraySize * bm_execution::multiplicatorMap[v.first] / minTime) * 1.0e-6
                << std::setw(ENTRY_SPACE) << avgTime
                << std::setw(ENTRY_SPACE) << minTime
                << std::setw(ENTRY_SPACE) << maxTime << std::endl;
    }

}

std::unique_ptr<stream::StreamData>
stream::StreamBenchmark::generateInputData() {
    auto d = std::unique_ptr<stream::StreamData>(new StreamData(*executionSettings->context, executionSettings->programSettings->streamArraySize));
    for (int i=0; i< executionSettings->programSettings->streamArraySize; i++) {
        d->A[i] = 1.0;
        d->B[i] = 2.0;
        d->C[i] = 0.0;
    }

    return d;
}

bool  
stream::StreamBenchmark::validateOutputAndPrintError(stream::StreamData &data) {
    HOST_DATA_TYPE aj,bj,cj,scalar;
    HOST_DATA_TYPE aSumErr,bSumErr,cSumErr;
    HOST_DATA_TYPE aAvgErr,bAvgErr,cAvgErr;
    double epsilon;
    ssize_t	j;
    int	k,ierr,err;

    /* reproduce initialization */
    aj = 1.0;
    bj = 2.0;
    cj = 0.0;
    /* a[] is modified during timing check */
    aj = 2.0E0 * aj;
    /* now execute timing loop */
    scalar = 3.0;
    for (k=0; k<executionSettings->programSettings->numRepetitions; k++)
    {
        cj = aj;
        bj = scalar*cj;
        cj = aj+bj;
        aj = bj+scalar*cj;
    }

    /* accumulate deltas between observed and expected results */
    aSumErr = 0.0;
    bSumErr = 0.0;
    cSumErr = 0.0;
    for (j=0; j< executionSettings->programSettings->streamArraySize; j++) {
        aSumErr += abs(data.A[j] - aj);
        bSumErr += abs(data.B[j] - bj);
        cSumErr += abs(data.C[j] - cj);
        // if (j == 417) printf("Index 417: c[j]: %f, cj: %f\n",c[j],cj);	// MCCALPIN
    }
    aAvgErr = aSumErr / (HOST_DATA_TYPE) executionSettings->programSettings->streamArraySize;
    bAvgErr = bSumErr / (HOST_DATA_TYPE) executionSettings->programSettings->streamArraySize;
    cAvgErr = cSumErr / (HOST_DATA_TYPE) executionSettings->programSettings->streamArraySize;

    if (sizeof(HOST_DATA_TYPE) == 4) {
        epsilon = 1.e-6;
    }
    else if (sizeof(HOST_DATA_TYPE) == 8) {
        epsilon = 1.e-13;
    }
    else {
        printf("WEIRD: sizeof(STREAM_TYPE) = %lu\n",sizeof(executionSettings->programSettings->streamArraySize));
        epsilon = 1.e-6;
    }

    err = 0;
    if (abs(aAvgErr/aj) > epsilon) {
        err++;
        printf ("Failed Validation on array a[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
        printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",aj,aAvgErr,abs(aAvgErr)/aj);
        ierr = 0;
        for (j=0; j<executionSettings->programSettings->streamArraySize; j++) {
            if (abs(data.A[j]/aj-1.0) > epsilon) {
                ierr++;
            }
        }
        printf("     For array a[], %d errors were found.\n",ierr);
    }
    if (abs(bAvgErr/bj) > epsilon) {
        err++;
        printf ("Failed Validation on array b[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
        printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",bj,bAvgErr,abs(bAvgErr)/bj);
        printf ("     AvgRelAbsErr > Epsilon (%e)\n",epsilon);
        ierr = 0;
        for (j=0; j<executionSettings->programSettings->streamArraySize; j++) {
            if (abs(data.B[j]/bj-1.0) > epsilon) {
                ierr++;
            }
        }
        printf("     For array b[], %d errors were found.\n",ierr);
    }
    if (abs(cAvgErr/cj) > epsilon) {
        err++;
        printf ("Failed Validation on array c[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
        printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",cj,cAvgErr,abs(cAvgErr)/cj);
        printf ("     AvgRelAbsErr > Epsilon (%e)\n",epsilon);
        ierr = 0;
        for (j=0; j<executionSettings->programSettings->streamArraySize; j++) {
            if (abs(data.C[j]/cj-1.0) > epsilon) {
                ierr++;
            }
        }
        printf("     For array c[], %d errors were found.\n",ierr);
    }
    if (err == 0) {
        printf ("Solution Validates: avg error less than %e on all three arrays\n",epsilon);
        return true;
    }
    return false;
}