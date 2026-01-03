#pragma once
#include<cstring>
#include<list>
#include<memory>
#include<mutex>
#include<unordered_map>
using namespace std;
class LRUCache
{
private:
    struct Node{
        int key;
        int value;
        Node *pre, *next;
        //* Default Construction 
        Node(int x, int y){
            key = x;
            value = y;

            pre = nullptr;
            next = nullptr;
            }
    };
    //* Capacity
    int n;
    //* Hash map, <int, Node *> is used to interact with doubly linked list

    unordered_map<int, Node *> hash;

    //* Flag node
    Node *L, *R;
    //* Remove the key in the map
    void remove(Node* node){
        Node *pre = node->pre;
        Node *next = node->next;
        pre->next = next;
        next->pre = pre;
        hash.erase(node->key);
    }

    void insert(int key, int value){
        //* Get the most recent node, which is the tail *R pre node
        Node *pre1 = R->pre;
        Node *next1 = R;
        //* Create a new node
        Node *newNode = new Node(key, value);
        
        //* Add in the pre1->next place
        pre1->next = newNode;
        newNode->next = next1;
        //* Add in the next1->pre place
        next1->pre = newNode;
        newNode->pre = pre1;

        hash[key] = newNode;
    }

public:
    //* Initialized the Cache space
    LRUCache(int capacity){
        n = capacity;
        L = new Node(-1, -1);
        R = new Node(-1, -1);
    //* Connet two flags node
        L->next = R;
        R->pre = L;
    }

    int get(int key){
        //* Cache exist such data
        if(hash.find(key)!=hash.end()){
            Node *node = hash[key];
            remove(node);

            insert(node->key, node->value);
            return node->value;
        }
        else
            return -1;
    }

    void put(int key, int value){
        if(hash.find(key)!=hash.end()){
            //* Get the exist node by hash
            Node *node = hash[key];
            //* Remove the exist node, then add it to the most recent place
            remove(node);
            insert(key, value);
        }
        else{
            //* If the hash map is full, remove the last called node, then add the new node to the recent place
            if(hash.size()==n){
                Node *node = L->next;
                remove(node);
                insert(key, value);
            }
            else
                insert(key, value);
        }
    }
};
