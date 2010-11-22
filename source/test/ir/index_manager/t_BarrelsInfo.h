/**
* @file       t_BarrelsInfo.h
* @author     Jun
* @version    SF1 v5.0
* @brief Test cases on @c BarrelsInfo.
*
*/

#ifndef T_BARRELS_INFO_H
#define T_BARRELS_INFO_H

#include <string>
#include <glog/logging.h>
#include <boost/test/unit_test.hpp>

#include <ir/index_manager/index/IndexReader.h>
#include <ir/index_manager/index/BarrelInfo.h>

#include "IndexerTestFixture.h"

namespace t_BarrelsInfo
{
/**
 * Create barrels and check barrel status.
 */
inline void index(const IndexerTestConfig& config)
{
    VLOG(2) << "=> t_BarrelsInfo::index";

    IndexerTestFixture fixture;
    IndexerTestConfig newConfig(config);
    // not to merge when offline mode, in order to check each barrel
    if(newConfig.indexMode_.find("default") != std::string::npos)
        newConfig.isMerge_ = false;
    fixture.configTest(newConfig);

    const int barrelNum = config.iterNum_;
    for(int i=0; i<barrelNum; ++i)
        fixture.createDocument(); // create barrel i

    Indexer* pIndexer = fixture.getIndexer();
    IndexReader* pIndexReader = pIndexer->getIndexReader();
    BarrelsInfo* pBarrelsInfo = pIndexReader->getBarrelsInfo();

    BOOST_CHECK_EQUAL(pBarrelsInfo->maxDocId(), fixture.getMaxDocID());
    BOOST_CHECK_EQUAL(pBarrelsInfo->getDocCount(), fixture.getDocCount());

    const unsigned int newDocNum = config.docNum_;
    if(pIndexer->getIndexManagerConfig()->indexStrategy_.indexMode_ != IndexerTestFixture::INDEX_MODE_REALTIME)
    {
        BOOST_CHECK_EQUAL(pBarrelsInfo->getBarrelCount(), barrelNum);

        for(int i=0; i<barrelNum; ++i)
        {
            BarrelInfo* pBarrelInfo = (*pBarrelsInfo)[i];
            BOOST_CHECK_EQUAL(pBarrelInfo->getDocCount(), newDocNum);
            BOOST_CHECK_EQUAL(pBarrelInfo->getBaseDocID(), i*newDocNum+1);
            BOOST_CHECK_EQUAL(pBarrelInfo->getMaxDocID(), (i+1)*newDocNum);
        }
    }

    VLOG(2) << "<= t_BarrelsInfo::index";
}

/**
 * Create barrels, optimize barrels (merge them into one), and check barrel status.
 */
inline void optimize(const IndexerTestConfig& config)
{
    VLOG(2) << "=> t_BarrelsInfo::optimize";

    IndexerTestFixture fixture;
    fixture.configTest(config);

    const int barrelNum = config.iterNum_;
    for(int i=0; i<barrelNum; ++i)
        fixture.createDocument(); // create barrel i

    Indexer* pIndexer = fixture.getIndexer();
    pIndexer->optimizeIndex();

    // wait for merge finish
    pIndexer->waitForMergeFinish();

    IndexReader* pIndexReader = pIndexer->getIndexReader();
    BarrelsInfo* pBarrelsInfo = pIndexReader->getBarrelsInfo();

    BOOST_CHECK_EQUAL(pBarrelsInfo->maxDocId(), fixture.getMaxDocID());
    BOOST_CHECK_EQUAL(pBarrelsInfo->getDocCount(), fixture.getDocCount());
    BOOST_CHECK_EQUAL(pBarrelsInfo->getBarrelCount(), 1);

    VLOG(2) << "<= t_BarrelsInfo::optimize";
}

/**
 * Create barrels, optimize barrels (merge them into one), and create barrels again, then check barrel status.
 */
inline void createAfterOptimize(const IndexerTestConfig& config)
{
    VLOG(2) << "=> t_BarrelsInfo::createAfterOptimize";

    IndexerTestFixture fixture;
    fixture.configTest(config);

    const int barrelNum = config.iterNum_;
    for(int i=0; i<barrelNum; ++i)
        fixture.createDocument(); // create barrel i

    Indexer* pIndexer = fixture.getIndexer();
    pIndexer->optimizeIndex();

    for(int i=0; i<barrelNum; ++i)
        fixture.createDocument(); // create barrel i

    // wait for merge finish
    pIndexer->waitForMergeFinish();

    IndexReader* pIndexReader = pIndexer->getIndexReader();
    BarrelsInfo* pBarrelsInfo = pIndexReader->getBarrelsInfo();

    BOOST_CHECK_EQUAL(pBarrelsInfo->maxDocId(), fixture.getMaxDocID());
    BOOST_CHECK_EQUAL(pBarrelsInfo->getDocCount(), fixture.getDocCount());

    BOOST_CHECK_GE(pBarrelsInfo->getBarrelCount(), 1);
    if(pIndexer->getIndexManagerConfig()->indexStrategy_.indexMode_ != IndexerTestFixture::INDEX_MODE_REALTIME)
        BOOST_CHECK_LE(pBarrelsInfo->getBarrelCount(), barrelNum+1);

    VLOG(2) << "<= t_BarrelsInfo::createAfterOptimize";
}

/**
 * Pause and resume the merge.
 */
inline void pauseResumeMerge(const IndexerTestConfig& config)
{
    VLOG(2) << "=> t_BarrelsInfo::pauseResumeMerge";

    IndexerTestFixture fixture;
    fixture.configTest(config);

    Indexer* pIndexer = fixture.getIndexer();
    pIndexer->pauseMerge();

    const int barrelNum = config.iterNum_;
    for(int i=0; i<barrelNum; ++i)
        fixture.createDocument(); // create barrel i

    IndexReader* pIndexReader = pIndexer->getIndexReader();
    BarrelsInfo* pBarrelsInfo = pIndexReader->getBarrelsInfo();
    BOOST_CHECK_EQUAL(pBarrelsInfo->maxDocId(), fixture.getMaxDocID());
    BOOST_CHECK_EQUAL(pBarrelsInfo->getDocCount(), fixture.getDocCount());

    if(pIndexer->getIndexManagerConfig()->indexStrategy_.indexMode_ == IndexerTestFixture::INDEX_MODE_REALTIME)
        BOOST_CHECK_GE(pBarrelsInfo->getBarrelCount(), barrelNum);
    else
        BOOST_CHECK_EQUAL(pBarrelsInfo->getBarrelCount(), barrelNum);

    pIndexer->resumeMerge();
    pIndexer->optimizeIndex();

    // wait for merge finish
    pIndexer->waitForMergeFinish();

    pIndexReader = pIndexer->getIndexReader();
    pBarrelsInfo = pIndexReader->getBarrelsInfo();

    BOOST_CHECK_EQUAL(pBarrelsInfo->getBarrelCount(), 1);
    BOOST_CHECK_EQUAL(pBarrelsInfo->maxDocId(), fixture.getMaxDocID());
    BOOST_CHECK_EQUAL(pBarrelsInfo->getDocCount(), fixture.getDocCount());

    VLOG(2) << "<= t_BarrelsInfo::pauseResumeMerge";
}

}
#endif
