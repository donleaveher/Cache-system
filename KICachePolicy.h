#pragma once
namespace KamaCache{

template<typename key, typename value>
class KICachePolicy{
    public:
        virtual ~KICachePolicy() {};
        //* Adding the interferance of Cache
        virtual void put(key k, value v) = 0;
        //* key是传入参数  访问到的值以传出参数的形式返回 | 访问成功返回true
        virtual bool get(key k, value &v) = 0;
        //* 如果缓存中找到了key 返回value
        virtual value get(key k) = 0;
    };
}