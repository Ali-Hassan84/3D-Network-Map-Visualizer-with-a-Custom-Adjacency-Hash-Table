#include "node.h"

Node* NodeCreate(const std::string& id, Vec3 pos) {
    Node* n = new Node();
    n->id = id;
    n->pos = pos;
    n->scene = pos;
    n->index = -1;
    n->component = -1;
    return n;
}

void NodeDestroy(Node* n) {
    if (!n) return;
    DynArrayFree(n->out);
    DynArrayFree(n->in);
    delete n;
}
