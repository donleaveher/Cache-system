#pragma once

#include "../KICachePolicy.h"
#include "KArcLruPart.h"
#include "KArcLfuPart.h"
#include <memory>

namespace KamaCache{

template<typename Key, typename Value>
class KArcCache: public KICachePolicy<Key, Value>{
    public:
        explicit KArcCache(size_t capacity=10, size_t transform=2):
            capacity_(capacity),
            transformThreshold_(transform),
            lruPart(std::make_unique<ArcLruPart<Key,Value>>(capacity, transform)),
            lfuPart(std::make_unique<ArcLfuPart<Key,Value>>(capacity, transform))
        {}

        ~KArcCache() override = default;

        void put(Key key, Value value) override{
            checkGhostCaches(key);
            bool inLfu = lfuPart->contain(key);
            lruPart->put(key, value);
            if(inLfu){
                lfuPart->put(key, value);
            }
        }

        bool get(Key key, Value& value)override{
            checkGhostCaches(key);
            bool shouldtransform = false;
            if(lruPart->get(key, value, shouldtransform)){
                if(shouldtransform){
                    lruPart->remove(key);
                    lfuPart->put(key, value);
                }
                return true;
            }
            return lfuPart->get(key, value);
        }

        Value get(Key key) override{
            Value value{};
            get(key, value);
            return value;
        }

    private:
        bool checkGhostCaches(Key key){
            bool inGhost = false;
            if(lruPart->checkGhost(key)){
                if(lruPart->decreaseCapacity()){
                    lfuPart->increaseCapacity();
                }
                inGhost = true;
            }
            else if(lfuPart->checkGhost(key)){
                if(lfuPart->decreaseCapacity()){
                    lruPart->increaseCapacity();
                }
                inGhost = true;
            }
            return inGhost;
        }

    private:
        size_t capacity_;
        size_t transformThreshold_;
        std::unique_ptr<ArcLruPart<Key, Value>> lruPart;
        std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart;
};
}