
#pragma once

#include "KArcCacheNode.h"
#include<mutex>
#include<map>
#include<unordered_map> 
#include<list>
namespace KamaCache{

template<typename Key, typename Value>
class ArcLfuPart{
    public:
        using NodeType = ArcNode<Key, Value>;
        using NodePtr = std::shared_ptr<NodeType>;
        using NodeMap = std::unordered_map<Key, NodePtr>;
        using FreqMap = std::map<size_t, std::list<NodePtr>>;
        
        explicit ArcLfuPart(size_t cap, size_t transform):
            capacity_(cap),
            ghostCapacity_(cap),
            transfromThreshold_(transform),
            minFreq_(0){
            initialize();
        }
        
        bool put(Key key, Value value){
            if(capacity_ == 0){
                return false;
            }
            std::lock_guard<std::mutex> lock(mutex);
            auto it = mainCache_.find(key);
            if(it != mainCache_.end()){
                return updateExistingNode(it->second, value);
            }
            return addNewNode(key, value);
        }

        bool get(Key key, Value& value){
            std::lock_guard<std::mutex> lock(mutex);
            auto it = mainCache_.find(key);
            if(it!=mainCache_.end()){
                updateNodeFrequency(it->second);
                value = it->second->getValue();
                return true;
            }
            return false;
        }

        void increaseCapacity() { 
            std::lock_guard<std::mutex> lock(mutex);
            capacity_++; 
            ghostCapacity_++;
        }
        bool decreaseCapacity(){
            std::lock_guard<std::mutex> lock(mutex);
            if(capacity_<=0)
                return false;
            if(mainCache_.size() == capacity_){
                evictLeastFreqent();
            }
            capacity_--;
            if(ghostCapacity_>0){
                ghostCapacity_--;
            }
            return true;
        }

        bool contain(Key key){
            std::lock_guard<std::mutex> lock(mutex);
            return mainCache_.find(key) != mainCache_.end();
        }
        bool checkGhost(Key key){
            std::lock_guard<std::mutex> lock(mutex);
            auto it = ghostCache_.find(key);
            if(it != ghostCache_.end()){
                removeFromGhost(it->second);
                ghostCache_.erase(it);
                return true;
            }
            return false;
        }

    private:
        void initialize(){
            ghostHead_ = std::make_shared<NodeType>();
            ghostTail_ = std::make_shared<NodeType>();
            ghostHead_->next_ = ghostTail_;
            ghostTail_->prev_ = ghostHead_;
        }

        bool updateExistingNode(NodePtr node, const Value& value){
            node->setValue(value);
            updateNodeFrequency(node);
            return true;
        }

        bool addNewNode(const Key& key, const Value& value){
            if(mainCache_.size()>=capacity_){
                evictLeastFreqent();
            }
            NodePtr newNode = std::make_shared<NodeType>(key, value);
            if(freqMap_.find(1)==freqMap_.end()){
                freqMap_[1] = std::list<NodePtr>();
            }
            freqMap_[1].push_back(newNode);
            mainCache_[key] = newNode;
            minFreq_ = 1;

            return true;
        }

        void updateNodeFrequency(NodePtr node){
            size_t oldFreq = node->getAccessCount();
            node->incrementAccessCount();
            size_t newFreq = node->getAccessCount();

            auto &oldList = freqMap_[oldFreq];
            oldList.remove(node);
            if(oldList.empty()){
                freqMap_.erase(oldFreq);
                if(oldFreq == minFreq_)
                    minFreq_ = newFreq;
            }
            if(freqMap_.find(newFreq) == freqMap_.end()){
                freqMap_[newFreq] = std::list<NodePtr>();
            }

            freqMap_[newFreq].push_back(node);
        }

        void evictLeastFreqent(){
            if(freqMap_.empty())
                return;

            auto& minfreqList = freqMap_[minFreq_];
            if(minfreqList.empty())
                return;

            NodePtr leastnode = minfreqList.front();
            minfreqList.pop_front();

            if(minfreqList.empty()){
                freqMap_.erase(minFreq_);
                minFreq_ = freqMap_.empty() ? 0 : freqMap_.begin()->first;
            }

            if(ghostCache_.size()>=ghostCapacity_){
                removeOldestGhost();
            }
            addToGhost(leastnode);
            mainCache_.erase(leastnode->getKey());
        }

        void addToGhost(NodePtr node){
            node->next_ = ghostHead_->next_;
            node->prev_ = ghostHead_;
            ghostHead_->next_->prev_ = node;
            ghostHead_->next_ = node;
            ghostCache_[node->getKey()] = node;
        }
        void removeFromGhost(NodePtr node){
            if(!node->prev_.expired()&&node->next_){
                auto prev = node->prev_.lock();
                prev->next_ = node->next_;
                node->next_->prev_ = node->prev_;
                node->next_ = nullptr;
            }
        }
        void removeOldestGhost(){
            NodePtr oldestGhost = ghostTail_->prev_.lock();
            if(oldestGhost && oldestGhost!=ghostHead_){
                removeFromGhost(oldestGhost);
                ghostCache_.erase(oldestGhost->getKey());
            }
        }

    private:
        size_t capacity_;
        size_t ghostCapacity_;
        size_t transfromThreshold_;
        size_t minFreq_;
        std::mutex mutex;

        NodeMap mainCache_;
        NodeMap ghostCache_;
        FreqMap freqMap_;

        NodePtr ghostHead_;
        NodePtr ghostTail_;
};
}
