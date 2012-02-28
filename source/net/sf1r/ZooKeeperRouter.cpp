/* 
 * File:   ZooKeeperRouter.cpp
 * Author: Paolo D'Apice
 * 
 * Created on February 17, 2012, 2:32 PM
 */

#include "net/sf1r/ZooKeeperRouter.hpp"
#include "net/sf1r/Sf1Topology.hpp"
#include "Sf1Watcher.hpp"
#include "ZooKeeperNamespace.hpp"
#include "util/kv2string.h"
#include <boost/foreach.hpp>
#include <boost/regex.hpp>
#include <glog/logging.h>


NS_IZENELIB_SF1R_BEGIN

using iz::ZooKeeper;
using std::string;
using std::vector;


namespace {

const bool AUTO_RECONNECT = true;
typedef vector<string> strvector;

const std::string ROOT_NODE = "/";
const std::string  TOPOLOGY = "/SearchTopology";

const boost::regex TPLG_REGEX("\\/SF1R-\\w+\\d?");
const boost::regex NODE_REGEX("\\/SF1R-\\w+\\d*\\/SearchTopology\\/Replica\\d+\\/Node\\d+");


}


ZooKeeperRouter::ZooKeeperRouter(const string& hosts, int recvTimeout) { 
    // 0. connect to ZooKeeper
    LOG(INFO) << "Connecting to ZooKeeper servers:" << hosts;
    client = new iz::ZooKeeper(hosts, recvTimeout, AUTO_RECONNECT);
    
    // 1. register a watcher that will control the Sf1Driver instances
    LOG(INFO) << "Registering watcher ...";
    watcher = new Sf1Watcher(this);
    client->registerEventHandler(watcher);
    
    // 2. obtain the list of actually running SF1 servers
    LOG(INFO) << "Getting actual topology ...";
    topology = new Sf1Topology;
    loadTopology();
    
    // 3. connect to each of them using the Sf1Driver class
    // TODO
    
    LOG(INFO) << "ZooKeeperRouter ready";
}


ZooKeeperRouter::~ZooKeeperRouter() {
    delete client;
    delete watcher;
    delete topology;
}


void
ZooKeeperRouter::loadTopology() {
    strvector clusters;
    client->getZNodeChildren(ROOT_NODE, clusters, ZooKeeper::WATCH);
    
    BOOST_FOREACH(string s, clusters) {
        DLOG(INFO) << "found: " << s;
        string cluster = s + TOPOLOGY;
        addClusterNode(cluster);
    }
}


void
ZooKeeperRouter::addClusterNode(const string& cluster) {
    if (client->isZNodeExists(cluster, ZooKeeper::WATCH)) {
        DLOG(INFO) << "cluster: " << cluster;
        
        strvector replicas;
        client->getZNodeChildren(cluster, replicas, ZooKeeper::WATCH);

        BOOST_FOREACH(string replica, replicas) {
            DLOG(INFO) << "replica: " << replica;
            strvector nodes;
            client->getZNodeChildren(replica, nodes, ZooKeeper::WATCH);

            BOOST_FOREACH(string path, nodes) {
                addSf1Node(path);
            }
        }
    }
}


void
ZooKeeperRouter::addSf1Node(const string& path) {
    boost::lock_guard<boost::mutex> lock(mutex);

    LOG(INFO) << "sf1 node: " << path;
    
    if (topology->isPresent(path)) return;
    
    string data;
    client->getZNodeData(path, data, ZooKeeper::WATCH);
    LOG(INFO) << "node data: " << data;

    topology->addNode(path, data);
}


void 
ZooKeeperRouter::updateNodeData(const std::string& path) {
    boost::lock_guard<boost::mutex> lock(mutex);

    string data;
    client->getZNodeData(path, data, iz::ZooKeeper::WATCH);
    LOG(INFO) << "node data: " << data;
    
    topology->updateNode(path, data);
}


void
ZooKeeperRouter::removeClusterNode(const string& path) {
    boost::lock_guard<boost::mutex> lock(mutex);
    
    DLOG(INFO) << "cluster: " << path;
    topology->removeNode(path);
}


void
ZooKeeperRouter::watchChildren(const std::string& path) {
    strvector children;
    client->getZNodeChildren(path, children, ZooKeeper::WATCH);
    DLOG(INFO) << "Watching children of: " << path;
    BOOST_FOREACH(string s, children) {
        DLOG(INFO) << "child: " << s;
        if (boost::regex_match(s, NODE_REGEX)) {
            addSf1Node(s);
        } else {
            if (boost::regex_search(s, TPLG_REGEX)) {
                watchChildren(s);
            }
        }
    }
}


vector<Sf1Node>*
ZooKeeperRouter::getSf1Nodes() const {
    vector<Sf1Node>* nodes = new vector<Sf1Node>;
    topology->getNodes(*nodes);
    return nodes;
}


vector<Sf1Node>*
ZooKeeperRouter::getSf1Nodes(const std::string& collection) const {
    vector<Sf1Node>* nodes = new vector<Sf1Node>;
    topology->getNodes(*nodes, collection);
    return nodes;
}

NS_IZENELIB_SF1R_END
