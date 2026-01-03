#pragma once 

#include <cmath>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "KICachePolicy.h"
#include<thread>
namespace KamaCache{

//* 前向声明
template <typename key, typename value>class KLruCache;
template<typename key, typename value>
class LruNode{
    private:
        key key_;
        value value_;
        size_t accessCount;
        std::weak_ptr<LruNode<key, value>> prev_;
        std::shared_ptr<LruNode<key, value>> next_;

    public:
        LruNode(key k, value v):
        key_(k),
        value_(v),
        accessCount(1){}

        key getKey() const { return key_; }
        value getValue() const { return value_; }
        void setValue(const value &v) { value_ = v; }
        size_t getAccessCount() const { return accessCount; }
        void incrementAccessCount() { accessCount++ ; }

        friend class KLruCache<key, value>;
};

template<typename key, typename value>
class KLruCache: public KICachePolicy<key, value>{
    public:
        using LruNodeType = LruNode<key, value>;
        using Nodeptr = std::shared_ptr<LruNodeType>;
        using NodeMap = std::unordered_map<key, Nodeptr>;

    private:
        int capacity_;
        NodeMap nodeMap_;
        std::mutex mtx;
        Nodeptr dummyHead_;
        Nodeptr dummyTail_;

        void initializeList(){
            dummyHead_ = std::make_shared<LruNodeType>(key(), value());
            dummyTail_ = std::make_shared<LruNodeType>(key(), value());
            dummyHead_->next_ = dummyTail_;
            dummyTail_->prev_ = dummyHead_;
        }

        void insertNode(Nodeptr node){
            node->next_ = dummyTail_;
            node->prev_  = dummyTail_->prev_;
            dummyTail_->prev_.lock()->next_ = node;
            dummyTail_->prev_ = node;
        }

        void removeNode(Nodeptr node){
            if(!node->prev_.expired()&&node->next_){
                auto prev = node->prev_.lock();
                prev->next_ = node->next_;
                node->next_->prev_ = prev;
                node->next_ = nullptr;
            }
        }

        void moveToMostRecent(Nodeptr node){
            removeNode(node);
            insertNode(node);
        }

        void updateExistingNode(Nodeptr node, const value& v){
            node->setValue(v);
            moveToMostRecent(node);
        }

        void evictLeastRecent(){
            Nodeptr leastRecent = dummyHead_->next_;
            removeNode(leastRecent);
            nodeMap_.erase(leastRecent->getKey());
        }

        void addNewNode(const key& k, const value& v){
            if(nodeMap_.size() >= capacity_){
                evictLeastRecent();
            }
            Nodeptr node = std::make_shared<LruNodeType>(k, v);
            insertNode(node);
            nodeMap_[k] = node;
        }

    public: 
        KLruCache(int capacity) : capacity_(capacity) { initializeList(); }

        ~KLruCache() override = default;

        void put(key k, value v)override{
            if(capacity_<=0)
                return;
            std::lock_guard<std::mutex> lock(mtx);
            auto it = nodeMap_.find(k);
            if(it!= nodeMap_.end()){
                updateExistingNode(it->second, v);
                return;
            }
            addNewNode(k, v);
        }

        void remove(key k){
            std::lock_guard<std::mutex> lock(mtx);
            auto it = nodeMap_.find(k);
            if(it != nodeMap_.end()){
                removeNode(it->second);
                nodeMap_.erase(it);
            }
        }
        
        bool get(key k, value& v)override{
            std::lock_guard<std::mutex> lock(mtx);
            auto it = nodeMap_.find(k);
            if(it!=nodeMap_.end()){
                moveToMostRecent(it->second);
                v = it->second->getValue();
                return true;
            }
            return false;
        }

        value get(key k)override{
            value v{};
            get(k, v);
            return v;
        }
};

template<typename key, typename value>
class KLrukCache: public KLruCache<key, value>{
    public:
        KLrukCache(int capacity, int historycapacity, int k):
        KLruCache<key,value>(capacity),
        historyList_(std::make_unique<KLruCache<key,size_t>>(historycapacity)),
        k_(k){}

        void put(key k, value v) override{
            std::lock_guard<std::mutex> lock(mtx_);
            value existvalue{};
            bool inMainCache = KLruCache<key, value>::get(k, existvalue);
            
            if(inMainCache){
                KLruCache<key, value>::put(k, v);
                return;
            }
            size_t historycount = historyList_->get(k);
            historycount++;
            
            historyValuemap_[k] = v;
            
            if(historycount>=k_){
                historyList_->remove(k);
                historyValuemap_.erase(k);
                KLruCache<key, value>::put(k,v);
            }else{
                historyList_->put(k, historycount);
            }
        }

        bool get(key k, value& v) override{
            std::lock_guard<std::mutex> lock(mtx_);
            bool inMainCache = KLruCache<key, value>::get(k, v);

            size_t historycount = historyList_->get(k);
            historycount++;
            historyList_->put(k, historycount);

            if(inMainCache){
                return true;
            }

            if(historycount>=k_){
                auto it = historyValuemap_.find(k);
                if(it!=historyValuemap_.end()){
                    value storeValue = it->second;
                    
                    historyList_->remove(k);
                    historyValuemap_.erase(it);

                    KLruCache<key, value>::put(k, storeValue);

                    v = storeValue;
                    return true;
                }
            }
            return false;
        }

        value get(key k) override{
            value v{};
            get(k, v);
            return v;
        }

    private:
        int k_;
        std::unique_ptr<KLruCache<key, size_t>> historyList_;
        std::unordered_map<key, value> historyValuemap_;
        std::mutex mtx_;
};

template<typename key, typename value>
class KHashLruCaches{
    public:
        KHashLruCaches(size_t cap, int num){
            // 1. 先算清楚 sliceNum
            sliceNum = (num > 0) ? num : std::thread::hardware_concurrency();
            
            // 2. 再算每个分片的大小
            size_t sliceSize = std::ceil(cap / static_cast<double>(sliceNum));
            
            // 3. 循环创建
            for (int i = 0; i < sliceNum; i++) {
                // 使用 make_unique，更符合 C++14/17 标准
                lruSliceCaches.emplace_back(std::make_unique<KLruCache<key, value>>(sliceSize));
            }
        }

        void put(key k, value v){
            size_t sliceIndex = Hash(k) % sliceNum;
            lruSliceCaches[sliceIndex]->put(k, v);
        }

        bool get(key k, value& v){
            size_t sliceIndex = Hash(k) % sliceNum;
            return lruSliceCaches[sliceIndex]->get(k, v);
        }

        value get(key k){
            value v{};
            get(k, v);
            return v;
        }

    private:
        size_t Hash(key k){
            std::hash<key> hashFunc;
            return hashFunc(k);
        }

        int sliceNum;
        std::vector < std::unique_ptr < KLruCache<key, value>>> lruSliceCaches;
};
}
