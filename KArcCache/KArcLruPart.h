#pragma once

#include "KArcCacheNode.h"
#include<unordered_map>
#include<mutex>

namespace KamaCache{
template<typename Key, typename Value>
class ArcLruPart{
    public:
        using NodeType = ArcNode<Key, Value>;
        using NodePtr = std::shared_ptr<NodeType>;
        using NodeMap = std::unordered_map<Key, NodePtr>;

        explicit ArcLruPart(size_t capacity, size_t transoformThreshold):
            capacity_(capacity),
            transoformThreshold_(transoformThreshold),
            ghostCapacity_(capacity)
            {
                initializeList();
            }
        
        bool put(Key key, Value value){
            if(capacity_ == 0)
                return false;
            std::lock_guard<std::mutex> lock(mutex);
            auto it = mainCache_.find(key);
            if(it!=mainCache_.end()){
                return updateExistingNode(it->second, value);
            }    
            return addNewNode(key, value);
        }

        bool get(Key key, Value& value_, bool& shouldTransform_){
            std::lock_guard<std::mutex> lock(mutex);
            auto it = mainCache_.find(key);
            if (it != mainCache_.end())
            {
                shouldTransform_ = updateNodeAccess(it->second);
                value_ = it->second->getValue();
                return true;
            }
            return false;
        }

        bool checkGhost(Key key){
            std::lock_guard<std::mutex> lock(mutex);
            auto it = ghostCache_.find(key);
            if(it!=ghostCache_.end()){
                removeFromGhost(it->second);
                ghostCache_.erase(it);
                return true;
            }
            return false;
        }
        void remove(Key key){
            std::lock_guard<std::mutex> lock(mutex);
            auto it = mainCache_.find(key);
            if(it!=mainCache_.end()){
                removeFromMain(it->second);
                mainCache_.erase(it);
            }
        }
        void increaseCapacity() { 
            std::lock_guard<std::mutex> lock(mutex);
            capacity_++; 
            ghostCapacity_++;
        }

        bool decreaseCapacity(){
            std::lock_guard<std::mutex> lock(mutex);
            if(capacity_<=0){
                return false;
            }
            if(mainCache_.size()>=capacity_){
                evictLeastRecent();
            }
            capacity_--;
            if(ghostCapacity_>0){
                ghostCapacity_--;
            }
            return true;
        }

    private:
        void initializeList(){
            mainHead_ = std::make_shared<NodeType>();
            mainTail_ = std::make_shared<NodeType>();
            mainHead_->next_ = mainTail_;
            mainTail_->prev_ = mainHead_;

            ghostHead_ = std::make_shared<NodeType>();
            ghostTail_ = std::make_shared<NodeType>();
            ghostHead_->next_ = ghostTail_;
            ghostTail_->prev_ = ghostHead_;
        }

        bool updateExistingNode(NodePtr node, const Value& value){
            node->setValue(value);
            moveToFront(node);
            return true;
        }

        bool addNewNode(const Key& key, const Value& value){
            if(mainCache_.size()>=capacity_){
                evictLeastRecent();
            }
            
            NodePtr node = std::make_shared<NodeType>(key, value);
            addToFront(node);
            mainCache_[node->getKey()] = node;
            
            return true;
        }

        bool updateNodeAccess(NodePtr node){
            moveToFront(node);
            node->incrementAccessCount();
            return node->getAccessCount() >= transoformThreshold_;
        }

        void addToFront(NodePtr node){
            node->next_ = mainHead_->next_;
            node->prev_ = mainHead_;
            mainHead_->next_->prev_ = node;
            mainHead_->next_ = node;
        }


        void moveToFront(NodePtr node){
            removeFromMain(node);
            addToFront(node);
        }

        void evictLeastRecent(){
            NodePtr leastRecent = mainTail_->prev_.lock();
            if(!leastRecent||leastRecent==mainHead_)
                return;
            
            removeFromMain(leastRecent);

            if(ghostCapacity_<=ghostCache_.size()){
                removeOldestGhost();
            }
            addToGhost(leastRecent);
            mainCache_.erase(leastRecent->getKey());
        }

        void removeFromMain(NodePtr node){
            if(!node->prev_.expired()&&node->next_){
                auto prev = node->prev_.lock();
                prev->next_ = node->next_;
                node->next_->prev_ = node->prev_;
                node->next_ = nullptr;               
            }
        }

        void removeFromGhost(NodePtr node){
            if(!node->prev_.expired()&&node->next_){
                auto prev = node->prev_.lock();
                prev->next_ = node->next_;
                node->next_->prev_ = node->prev_;
                node->next_ = nullptr;               
            }
        }

        void addToGhost(NodePtr node){
            node->accessCount_ = 1;
            node->prev_ = ghostHead_;
            node->next_ = ghostHead_->next_;
            ghostHead_->next_->prev_ = node;
            ghostHead_->next_ = node;
            ghostCache_[node->getKey()] = node;
        }

        void removeOldestGhost(){
            NodePtr oldestGhost = ghostTail_->prev_.lock();
            if(!oldestGhost||oldestGhost == ghostHead_){
                return;
            }
            removeFromGhost(oldestGhost);
            ghostCache_.erase(oldestGhost->getKey());
        }

    private:
        size_t capacity_;
        size_t ghostCapacity_;
        size_t transoformThreshold_;
        std::mutex mutex;

        NodeMap mainCache_;
        NodeMap ghostCache_;

        NodePtr mainHead_;
        NodePtr mainTail_;

        NodePtr ghostHead_;
        NodePtr ghostTail_;
};
}
