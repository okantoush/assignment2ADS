// ============================================================================
//  A B-Tree of order 4 (minimum degree t = 2).
//  Each node stores 1..3 keys and has 2..4 children.
//
//  Features:
//      * Insertion (top-down, proactive splitting)
//      * Deletion  (CLRS style)
//      * Search
//      * In-order traversal
//      * Level-order (BFS) printing
//      * Split counting
//      * Logging to log.txt after every operation
//      * Persistence: SAVE / RESTORE (snapshot.dat)
//      * Command driver that reads input.txt and writes output.txt
//      * No STL containers (only <iostream>, <fstream>, <sstream>, <string>)
// ============================================================================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cctype>

using namespace std;

// ============================================================================
//  BTreeNode
// ============================================================================
class BTreeNode {
public:
    int *keys;              // array of keys                      (size 2t-1)
    int  t;                 // minimum degree
    BTreeNode **children;   // array of child pointers            (size 2t)
    int  n;                 // current number of keys
    bool leaf;              // true if node is a leaf

    static long splitCount; // global split counter

    BTreeNode(int _t, bool _leaf);
    ~BTreeNode();

    // Core operations
    void insertNonFull(int k);
    void splitChild(int i, BTreeNode *y);
    void traverse(ostream &out);
    BTreeNode* search(int k);
    int  findKey(int k);

    // Deletion helpers
    void removeKey(int k);
    void removeFromLeaf(int idx);
    void removeFromNonLeaf(int idx);
    int  getPredecessor(int idx);
    int  getSuccessor(int idx);
    void fillChild(int idx);
    void borrowFromPrev(int idx);
    void borrowFromNext(int idx);
    void mergeChildren(int idx);

    // Serialization
    void serialize(ofstream &out);
};

long BTreeNode::splitCount = 0;

BTreeNode::BTreeNode(int _t, bool _leaf) : t(_t), n(0), leaf(_leaf) {
    keys     = new int[2*t - 1];
    children = new BTreeNode*[2*t];
    for (int i = 0; i < 2*t; ++i) children[i] = nullptr;
}

BTreeNode::~BTreeNode() {
    delete[] keys;
    delete[] children;
}

void BTreeNode::traverse(ostream &out) {
    int i;
    for (i = 0; i < n; ++i) {
        if (!leaf && children[i]) children[i]->traverse(out);
        out << keys[i] << " ";
    }
    if (!leaf && children[i]) children[i]->traverse(out);
}

BTreeNode* BTreeNode::search(int k) {
    int i = 0;
    while (i < n && k > keys[i]) ++i;
    if (i < n && keys[i] == k) return this;
    if (leaf) return nullptr;
    return children[i]->search(k);
}

int BTreeNode::findKey(int k) {
    int idx = 0;
    while (idx < n && keys[idx] < k) ++idx;
    return idx;
}

// ------- splitChild --------
// Splits child y (children[i]) which is assumed full.
// The median key of y is promoted into *this* node at position i.
void BTreeNode::splitChild(int i, BTreeNode *y) {
    BTreeNode *z = new BTreeNode(y->t, y->leaf);
    z->n = t - 1;

    // Copy upper t-1 keys of y to z
    for (int j = 0; j < t - 1; ++j) z->keys[j] = y->keys[j + t];

    // Copy upper t children of y to z
    if (!y->leaf) {
        for (int j = 0; j < t; ++j) {
            z->children[j] = y->children[j + t];
            y->children[j + t] = nullptr;
        }
    }
    y->n = t - 1;

    // Shift children of *this* to make space for z
    for (int j = n; j >= i + 1; --j) children[j + 1] = children[j];
    children[i + 1] = z;

    // Shift keys of *this* to make space for median
    for (int j = n - 1; j >= i; --j) keys[j + 1] = keys[j];
    keys[i] = y->keys[t - 1];
    ++n;

    ++splitCount;
}

// ------- insertNonFull --------
void BTreeNode::insertNonFull(int k) {
    int i = n - 1;
    if (leaf) {
        while (i >= 0 && keys[i] > k) { keys[i+1] = keys[i]; --i; }
        keys[i+1] = k;
        ++n;
    } else {
        while (i >= 0 && keys[i] > k) --i;
        if (children[i+1]->n == 2*t - 1) {
            splitChild(i+1, children[i+1]);
            if (keys[i+1] < k) ++i;
        }
        children[i+1]->insertNonFull(k);
    }
}

// ------------------------------------------------------------------
//  DELETION (CLRS Chapter 18)
// ------------------------------------------------------------------
void BTreeNode::removeKey(int k) {
    int idx = findKey(k);
    if (idx < n && keys[idx] == k) {
        if (leaf) removeFromLeaf(idx);
        else      removeFromNonLeaf(idx);
    } else {
        if (leaf) return;                  // key not present
        bool flag = (idx == n);
        if (children[idx]->n < t) fillChild(idx);
        if (flag && idx > n) children[idx-1]->removeKey(k);
        else                 children[idx]->removeKey(k);
    }
}

void BTreeNode::removeFromLeaf(int idx) {
    for (int i = idx + 1; i < n; ++i) keys[i-1] = keys[i];
    --n;
}

void BTreeNode::removeFromNonLeaf(int idx) {
    int k = keys[idx];
    if (children[idx]->n >= t) {
        int pred = getPredecessor(idx);
        keys[idx] = pred;
        children[idx]->removeKey(pred);
    } else if (children[idx+1]->n >= t) {
        int succ = getSuccessor(idx);
        keys[idx] = succ;
        children[idx+1]->removeKey(succ);
    } else {
        mergeChildren(idx);
        children[idx]->removeKey(k);
    }
}

int BTreeNode::getPredecessor(int idx) {
    BTreeNode *cur = children[idx];
    while (!cur->leaf) cur = cur->children[cur->n];
    return cur->keys[cur->n - 1];
}

int BTreeNode::getSuccessor(int idx) {
    BTreeNode *cur = children[idx+1];
    while (!cur->leaf) cur = cur->children[0];
    return cur->keys[0];
}

void BTreeNode::fillChild(int idx) {
    if (idx != 0 && children[idx-1]->n >= t)      borrowFromPrev(idx);
    else if (idx != n && children[idx+1]->n >= t) borrowFromNext(idx);
    else {
        if (idx != n) mergeChildren(idx);
        else          mergeChildren(idx - 1);
    }
}

void BTreeNode::borrowFromPrev(int idx) {
    BTreeNode *child   = children[idx];
    BTreeNode *sibling = children[idx-1];
    for (int i = child->n - 1; i >= 0; --i) child->keys[i+1] = child->keys[i];
    if (!child->leaf) {
        for (int i = child->n; i >= 0; --i) child->children[i+1] = child->children[i];
    }
    child->keys[0] = keys[idx-1];
    if (!child->leaf) child->children[0] = sibling->children[sibling->n];
    keys[idx-1] = sibling->keys[sibling->n - 1];
    ++child->n;
    --sibling->n;
}

void BTreeNode::borrowFromNext(int idx) {
    BTreeNode *child   = children[idx];
    BTreeNode *sibling = children[idx+1];
    child->keys[child->n] = keys[idx];
    if (!child->leaf) child->children[child->n + 1] = sibling->children[0];
    keys[idx] = sibling->keys[0];
    for (int i = 1; i < sibling->n; ++i) sibling->keys[i-1] = sibling->keys[i];
    if (!sibling->leaf) {
        for (int i = 1; i <= sibling->n; ++i) sibling->children[i-1] = sibling->children[i];
    }
    ++child->n;
    --sibling->n;
}

void BTreeNode::mergeChildren(int idx) {
    BTreeNode *child   = children[idx];
    BTreeNode *sibling = children[idx+1];
    child->keys[t-1] = keys[idx];
    for (int i = 0; i < sibling->n; ++i) child->keys[i+t] = sibling->keys[i];
    if (!child->leaf) {
        for (int i = 0; i <= sibling->n; ++i) child->children[i+t] = sibling->children[i];
    }
    for (int i = idx + 1; i < n; ++i) keys[i-1] = keys[i];
    for (int i = idx + 2; i <= n; ++i) children[i-1] = children[i];
    child->n += sibling->n + 1;
    --n;
    delete sibling;
}

// ------------------------------------------------------------------
//  SERIALIZATION  (pre-order: leaf_flag  n  k1..kn  [children...])
// ------------------------------------------------------------------
void BTreeNode::serialize(ofstream &out) {
    out << (leaf ? 1 : 0) << ' ' << n;
    for (int i = 0; i < n; ++i) out << ' ' << keys[i];
    out << '\n';
    if (!leaf) {
        for (int i = 0; i <= n; ++i) {
            if (children[i]) children[i]->serialize(out);
        }
    }
}

// ============================================================================
//  BTree (wrapper)
// ============================================================================
class BTree {
public:
    BTreeNode *root;
    int t;

    BTree(int _t) : root(nullptr), t(_t) {}
    ~BTree() { destroy(root); }

    void destroy(BTreeNode *node) {
        if (!node) return;
        if (!node->leaf) {
            for (int i = 0; i <= node->n; ++i) destroy(node->children[i]);
        }
        delete node;
    }

    BTreeNode* search(int k) { return root ? root->search(k) : nullptr; }

    void traverse(ostream &out) {
        if (root) root->traverse(out);
        out << '\n';
    }

    // ---- insert ------------------------------------------------
    void insert(int k) {
        if (!root) {
            root = new BTreeNode(t, true);
            root->keys[0] = k;
            root->n = 1;
        } else if (root->n == 2*t - 1) {
            BTreeNode *s = new BTreeNode(t, false);
            s->children[0] = root;
            s->splitChild(0, root);
            int i = 0;
            if (s->keys[0] < k) ++i;
            s->children[i]->insertNonFull(k);
            root = s;
        } else {
            root->insertNonFull(k);
        }
    }

    // ---- delete ------------------------------------------------
    void removeKey(int k) {
        if (!root) return;
        root->removeKey(k);
        if (root->n == 0) {
            BTreeNode *tmp = root;
            root = root->leaf ? nullptr : root->children[0];
            delete tmp;
        }
    }

    // ---- height ------------------------------------------------
    int height() const {
        int h = 0;
        BTreeNode *cur = root;
        if (!cur) return 0;
        while (!cur->leaf) { ++h; cur = cur->children[0]; }
        return h + 1;
    }

    // ---- level-order printing ----------------------------------
    void levelOrder(ostream &out) {
        if (!root) { out << "(empty)\n"; return; }

        const int MAXQ = 10000;
        BTreeNode **q = new BTreeNode*[MAXQ];
        int *lvl      = new int[MAXQ];
        int front = 0, back = 0;
        q[back] = root; lvl[back] = 0; ++back;
        int cur = -1;

        while (front < back) {
            BTreeNode *node = q[front];
            int lv = lvl[front];
            ++front;
            if (lv != cur) {
                if (cur != -1) out << '\n';
                out << "Level " << lv << ":";
                cur = lv;
            }
            out << " [";
            for (int i = 0; i < node->n; ++i) {
                out << node->keys[i];
                if (i < node->n - 1) out << ' ';
            }
            out << ']';
            if (!node->leaf) {
                for (int i = 0; i <= node->n; ++i) {
                    if (node->children[i]) {
                        q[back] = node->children[i];
                        lvl[back] = lv + 1;
                        ++back;
                    }
                }
            }
        }
        out << '\n';
        delete[] q;
        delete[] lvl;
    }

    // ---- persistence -------------------------------------------
    void save(const char *filename) {
        ofstream fout(filename);
        if (!fout) return;
        if (root) root->serialize(fout);
        fout.close();
    }

    BTreeNode* deserialize(ifstream &in) {
        int leafFlag, n;
        if (!(in >> leafFlag >> n)) return nullptr;
        BTreeNode *node = new BTreeNode(t, leafFlag == 1);
        node->n = n;
        for (int i = 0; i < n; ++i) in >> node->keys[i];
        if (!node->leaf) {
            for (int i = 0; i <= n; ++i) node->children[i] = deserialize(in);
        }
        return node;
    }

    void restore(const char *filename) {
        ifstream fin(filename);
        if (!fin) return;
        destroy(root);
        root = deserialize(fin);
        fin.close();
    }

    void clear() { destroy(root); root = nullptr; }
};

// ============================================================================
//  Helpers
// ============================================================================
static string toUpper(string s) {
    for (size_t i = 0; i < s.size(); ++i) s[i] = (char)toupper((unsigned char)s[i]);
    return s;
}

static void trim(string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) { s.clear(); return; }
    size_t b = s.find_last_not_of(" \t\r\n");
    s = s.substr(a, b - a + 1);
}

// ============================================================================
//  MAIN: command driver
// ============================================================================
int main() {
    BTree tree(2);                               // t = 2  ->  2-3-4 tree

    ifstream in("input.txt");
    if (!in) { cerr << "Error: cannot open input.txt\n"; return 1; }

    ofstream out("output.txt");
    ofstream log("log.txt");

    out << "=== B-Tree (2-3-4) Operation Trace ===\n\n";
    log << "=== B-Tree Operation Log ===\n\n";

    string line;
    int opNum = 0, invalid = 0;

    while (getline(in, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;

        istringstream iss(line);
        string tok; iss >> tok; tok = toUpper(tok);
        ++opNum;

        if (tok == "I") {
            int v;
            if (!(iss >> v)) { out << "[invalid I line] " << line << "\n"; ++invalid; continue; }
            tree.insert(v);
            out << "Insert " << v << ":\n";  tree.levelOrder(out); out << '\n';
            log << "Op " << opNum << ": Insert " << v
                << "  | splits=" << BTreeNode::splitCount
                << "  | height=" << tree.height() << '\n';
            tree.levelOrder(log); log << '\n';

        } else if (tok == "D") {
            int v;
            if (!(iss >> v)) { out << "[invalid D line] " << line << "\n"; ++invalid; continue; }
            tree.removeKey(v);
            out << "Delete " << v << ":\n"; tree.levelOrder(out); out << '\n';
            log << "Op " << opNum << ": Delete " << v
                << "  | height=" << tree.height() << '\n';
            tree.levelOrder(log); log << '\n';

        } else if (tok == "S") {
            int v;
            if (!(iss >> v)) { out << "[invalid S line] " << line << "\n"; ++invalid; continue; }
            BTreeNode *r = tree.search(v);
            out << "Search " << v << ": " << (r ? "FOUND" : "NOT FOUND") << "\n\n";
            log << "Op " << opNum << ": Search " << v
                << "  => " << (r ? "FOUND" : "NOT FOUND") << "\n\n";

        } else if (tok == "T") {
            out << "Traverse: "; tree.traverse(out); out << '\n';

        } else if (tok == "L") {
            out << "Level order:\n"; tree.levelOrder(out); out << '\n';

        } else if (tok == "SAVE") {
            tree.save("snapshot.dat");
            out << "SAVE -> snapshot.dat\n\n";
            log << "Op " << opNum << ": SAVE to snapshot.dat\n\n";

        } else if (tok == "RESTORE") {
            tree.restore("snapshot.dat");
            out << "RESTORE from snapshot.dat:\n"; tree.levelOrder(out); out << '\n';
            log << "Op " << opNum << ": RESTORE\n";
            tree.levelOrder(log); log << '\n';

        } else if (tok == "CLEAR") {
            tree.clear();
            out << "CLEAR: tree emptied.\n\n";
            log << "Op " << opNum << ": CLEAR\n\n";

        } else {
            out << "[unknown op] " << line << "\n";
            ++invalid;
        }
    }

    out << "\n=== Summary ===\n";
    out << "Total operations read : " << opNum << '\n';
    out << "Invalid lines ignored : " << invalid << '\n';
    out << "Total splits          : " << BTreeNode::splitCount << '\n';
    out << "Final height          : " << tree.height() << '\n';
    out << "Final in-order sweep  : "; tree.traverse(out);

    log << "\n=== Summary ===\n";
    log << "Total splits = " << BTreeNode::splitCount << '\n';
    log << "Final height = " << tree.height() << '\n';

    cout << "Done. Ops=" << opNum
         << "  splits=" << BTreeNode::splitCount
         << "  height=" << tree.height() << '\n';
    cout << "See output.txt and log.txt\n";

    in.close(); out.close(); log.close();
    return 0;
}
