#pragma once

#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <limits>
#include <vector>
#include "KICachePolicy.h"

namespace KamaCache{
template <typename Key, typename Value>
class KLfuCache;

template<typename Key, typename Value>
class FreqList{
    private:    
        struct Node{
            int freq;
            Key key;
            Value value;
            std::weak_ptr<Node> pre;
            std::shared_ptr<Node> next;
            //* Construction
            Node() : freq(1), next(nullptr){}
            Node(Key key, Value value):key(key),value(value),next(nullptr){}
        };
        using NodePtr = std::shared_ptr<Node>;
        int freq_;
        NodePtr head_;
        NodePtr tail_;

    public:
        explicit FreqList(int n):freq_(n){
            head_ = std::make_shared<Node>();
            tail_ = std::make_shared<Node>();
            head_->next = tail_;
            tail_->pre = head_;
        }

        bool isEmpty()const{
            //* if the head node next node is tail, means the list is empty
            return head_->next == tail_;
        }

        void addNode(NodePtr node){
            node->pre = tail_->pre;
            node->next = tail_;
            tail_->pre.lock()->next = node;
            tail_->pre = node;
        }

        void removeNode(NodePtr node){
            if(!node||!head_||!tail_){
                return;
            }
            if(node->pre.expired()||!node->next){
                return;
            }
            auto prev = node->pre.lock();
            prev->next = node->next;
            node->next->pre = node->pre;
            node->next = nullptr;
            node->pre.reset();
        }
        NodePtr getFirstNode() { return head_->next; }

        friend class KLfuCache<Key, Value>;
    };

template<typename Key, typename Value>
class KLfuCache:public KICachePolicy<Key, Value>{
    public:
        using Node = typename FreqList<Key, Value>::Node;
        using NodePtr = std::shared_ptr<Node>;
        using NodeMap = std::unordered_map<Key, NodePtr>;

        //* Construction Function
        KLfuCache(int capacity, int maxAverageNum = 1000000)
        : capacity_(capacity), minFreq_(1), maxAverageNum_(maxAverageNum),
            curAverageNum_(0), curTotalNum_(0){}
        //* Destroy Function
        ~KLfuCache() override = default;
        
        void put(Key key, Value value)override{
            if(capacity_ == 0){
                return;
            }
            std::lock_guard<std::mutex> lock(mutex);
            auto it = nodeMap_.find(key);
            if(it!=nodeMap_.end()){
                it->second->value = value;
                getInternal(it->second, value);
                return;
            }
            putInternal(key, value);
        }

        bool get(Key key, Value& value)override{
            std::lock_guard<std::mutex> lock(mutex);
            auto it = nodeMap_.find(key);
            if(it!=nodeMap_.end()){
                getInternal(it->second, value);
                return true;
            }
            return false;
        }

        Value get(Key key)override{
            Value value;
            get(key, value);
            return value;
        }

        void purge(){
            nodeMap_.clear();
            freqToFreqList_.clear();
            curTotalNum_ = 0;
            curAverageNum_ = 0;
            minFreq_ = 1;
        }

    private:
        void getInternal(NodePtr node, Value &value);
        void putInternal(Key key, Value value);
        
        void kickOut();
        
        void removeFromFreqList(NodePtr node);
        void addToFreqList(NodePtr node);
        
        void addFreqNum();
        void decreaseFreqNum(int num);
        void updateMinFreq();
        void handleOverMaxAverageNum();

    private:
        int capacity_;
        int minFreq_;
        int maxAverageNum_;
        int curAverageNum_;
        int curTotalNum_;
        std::mutex mutex;
        NodeMap nodeMap_;
        std::unordered_map<int, std::unique_ptr<FreqList<Key, Value>>> freqToFreqList_;
    };

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::getInternal(NodePtr node, Value& value){
        value = node->value;
        removeFromFreqList(node);
        node->freq++;
        addToFreqList(node);
        auto it = freqToFreqList_.find(node->freq - 1);
        if(node->freq - 1 == minFreq_ && (it==freqToFreqList_.end() || !it->second || it->second->isEmpty()))
            minFreq_++;
        addFreqNum();
    }
    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::putInternal(Key key, Value value){
        if(nodeMap_.size() == capacity_){
            kickOut();
        }
        NodePtr node = std::make_shared<Node>(key, value);
        nodeMap_[key] = node;
        addToFreqList(node);
        addFreqNum();
        minFreq_ = std::min(minFreq_, 1);
    }
    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::kickOut(){
        auto it = freqToFreqList_.find(minFreq_);
        if(it==freqToFreqList_.end() || !it->second || it->second->isEmpty()){
            updateMinFreq();
            it = freqToFreqList_.find(minFreq_);
            if(it==freqToFreqList_.end() || !it->second || it->second->isEmpty()){
                return;
            }
        }
        NodePtr node = it->second->getFirstNode();
        if(!node || node==it->second->tail_){
            return;
        }
        removeFromFreqList(node);
        nodeMap_.erase(node->key);
        decreaseFreqNum(node->freq);
        updateMinFreq();
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::removeFromFreqList(NodePtr node){
        if(!node){
            return;
        }
        auto fre = node->freq;
        auto it = freqToFreqList_.find(fre);
        if(it==freqToFreqList_.end() || !it->second){
            return;
        }
        it->second->removeNode(node);
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::addToFreqList(NodePtr node){
        if(!node)
            return;
        auto fre = node->freq;
        
        auto& listPtr = freqToFreqList_[fre];
        if(!listPtr){
            listPtr = std::make_unique<FreqList<Key, Value>>(fre);
        }

        listPtr->addNode(node);
    }
    
    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::decreaseFreqNum(int num){
        curTotalNum_ -= num;
        if(nodeMap_.empty()){
            curAverageNum_ = 0;
        }
        else
            curAverageNum_ = curTotalNum_ / nodeMap_.size();// Update the curAverageNum_ after decreaseFreqNum
    }
    
    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::updateMinFreq(){
        int newMin = std::numeric_limits<int>::max();
        for(const auto& pair:freqToFreqList_){
            if(pair.second && !pair.second->isEmpty()){
                newMin = std::min(newMin, pair.first);
            }
        }
        minFreq_ = (newMin == std::numeric_limits<int>::max()) ? 1 : newMin;
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::handleOverMaxAverageNum(){
        if(nodeMap_.empty())
            return;
        for (auto it = nodeMap_.begin(); it != nodeMap_.end(); it++)
        {
            if(!it->second){
                continue;
            }
            NodePtr node = it->second;
            //* Remove From the origin List, Then change the node's freq
            removeFromFreqList(node);
            node->freq -= maxAverageNum_ / 2;
            if(node->freq<1)
                node->freq = 1;
            //* Add the node back to the List
            addToFreqList(node);
        }
        //* Update the List newest MinFreq
        updateMinFreq();
    }

    template<typename Key, typename Value>
    void KLfuCache<Key, Value>::addFreqNum(){
        curTotalNum_++;
        if(nodeMap_.empty())
            curAverageNum_ = 0;
        else
            curAverageNum_ = curTotalNum_ / nodeMap_.size();
        //* If the count of AverageNum is oversize, reset it to its half
        if(curAverageNum_ > maxAverageNum_)
            handleOverMaxAverageNum();
    }

template<typename Key, typename Value>
class KHashLfuCache{

    public:
        //* Construction Function
        KHashLfuCache(size_t capacity, int num, int maxAverageNum = 10):
        capacity_(capacity),
        sliceNum_(num>0?num:std::thread::hardware_concurrency())
        {
            size_t sliceSize = std::ceil(capacity_ / static_cast<double>(sliceNum_));
            for (int i = 0; i < sliceNum_;i++){
                lfuSliceCache.emplace_back(new KLfuCache<Key, Value>(sliceSize, maxAverageNum));
            }
        }

        void put(Key key, Value value){
            size_t sliceIndex = Hash(key) % sliceNum_;
            lfuSliceCache[sliceIndex]->put(key, value);
        }

        bool get(Key key, Value& value){
            size_t sliceIndex = Hash(key) % sliceNum_;
            return lfuSliceCache[sliceIndex]->get(key, value);
        }

        Value get(Key key){
            Value value;
            get(key, value);
            return value;
        }

        void purge(){
            for(auto& it : lfuSliceCache){
                if(it){
                    it->purge();
                }
            }
        }

    private:
        size_t capacity_;
        int sliceNum_;
        std::vector<std::unique_ptr<KLfuCache<Key, Value>>> lfuSliceCache;
};
}
