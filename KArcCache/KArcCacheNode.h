#pragma once
#include<memory>

namespace KamaCache{

template<typename Key, typename Value>
class ArcNode{

    private:
        Key key_;
        Value value_;
        std::weak_ptr<ArcNode> prev_;
        std::shared_ptr<ArcNode> next_;
        
    public:
        size_t accessCount_;
        ArcNode():accessCount_(1),next_(nullptr){}
        ArcNode(Key key, Value value):key_(key),value_(value),accessCount_(1), next_(nullptr){}
        
        //* Getter
        Key getKey() const { return key_; }
        Value getValue() const { return value_; }
        size_t getAccessCount() const { return accessCount_; }

        //* Setter
        void setValue(Value value) { value_ = value; }
        void incrementAccessCount() { accessCount_++; }

        template<typename K, typename V> friend class ArcLruPart;
        template<typename K, typename V> friend class ArcLfuPart;
};

}