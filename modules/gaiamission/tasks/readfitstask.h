/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2018                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#ifndef __OPENSPACE_MODULE_GAIAMISSION___READFITSTASK___H__
#define __OPENSPACE_MODULE_GAIAMISSION___READFITSTASK___H__

#include <openspace/util/task.h>
#include <openspace/util/threadpool.h>
#include <openspace/util/concurrentjobmanager.h>
#include <modules/fitsfilereader/include/fitsfilereader.h>

namespace openspace {

namespace documentation { struct Documentation; }

class ReadFitsTask : public Task {
public:
    ReadFitsTask(const ghoul::Dictionary& dictionary);
    virtual ~ReadFitsTask();
    
    std::string description() override;
    void perform(const Task::ProgressCallback& onProgress) override;
    static documentation::Documentation Documentation();

private:
    const size_t MAX_SIZE_BEFORE_WRITE = 48000000; // ~183MB -> 2M stars with 24 values
    //const size_t MAX_SIZE_BEFORE_WRITE = 9000000; // ~34MB -> 500.000 stars with 18 values

    void readSingleFitsFile(const Task::ProgressCallback& progressCallback);
    void readAllFitsFilesFromFolder(const Task::ProgressCallback& progressCallback);
    int writeOctantToFile(const std::vector<float>& data, int index, 
        std::vector<bool>& isFirstWrite, int nValuesPerStar);

    std::string _inFileOrFolderPath;
    std::string _outFileOrFolderPath;
    bool _singleFileProcess;
    size_t _threadsToUse;
    int _firstRow;
    int _lastRow;
    std::vector<std::string> _allColumnNames;
    std::vector<std::string> _filterColumnNames;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_GAIAMISSION___READFITSTASK___H__
