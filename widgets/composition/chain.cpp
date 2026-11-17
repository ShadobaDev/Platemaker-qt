#include "chain.hpp"

#include <QHash>
#include <QSet>

namespace Chain {

namespace {

bool hasBody(const Balloon& b, int body)
{
    for (const Body& x : b.bodies)
        if (x.id == body)
            return true;
    return false;
}

//! The bodies joined to @p from through bridges, @p from among them.
QSet<int> reach(const Balloon& b, int from)
{
    QSet<int>  seen{from};
    QList<int> todo{from};
    while (!todo.isEmpty()) {
        const int at = todo.takeLast();
        for (const BalloonTail& t : b.tails) {
            if (!t.isBridge() || (t.root != at && t.to != at))
                continue;
            const int next = t.root == at ? t.to : t.root;
            if (!seen.contains(next)) {
                seen.insert(next);
                todo.append(next);
            }
        }
    }
    return seen;
}

//! @p c with only the bodies in @p keep, what hangs on them, and the words with no seat when @p captions.
Composition piece(const Composition& c, const QSet<int>& keep, bool captions)
{
    Composition p = c;
    Balloon&    b = *p.balloon;
    b.bodies.removeIf([&](const Body& x) { return !keep.contains(x.id); });
    b.tails.removeIf([&](const BalloonTail& t) {
        return !keep.contains(t.root) || (t.isBridge() && !keep.contains(t.to));
    });
    p.words.removeIf([&](const Words& w) { return w.seat == 0 ? !captions : !keep.contains(w.seat); });
    return p;
}

}  // namespace

int bridgesOn(const Balloon& b, int body)
{
    int n = 0;
    for (const BalloonTail& t : b.tails)
        if (t.isBridge() && (t.root == body || t.to == body))
            ++n;
    return n;
}

bool chainEnd(const Balloon& b, int body)
{
    return bridgesOn(b, body) < k_maxBridgesPerBody;
}

bool joined(const Balloon& b, int from, int to)
{
    return reach(b, from).contains(to);
}

QList<int> order(const Balloon& b)
{
    QList<int> out;
    // Ends first, so a chain is read from one of them; a lone body is an end too.
    QList<int> starts;
    for (const Body& x : b.bodies)
        if (bridgesOn(b, x.id) < k_maxBridgesPerBody)
            starts.append(x.id);
    for (const Body& x : b.bodies)
        starts.append(x.id);   // a ring has no end: whatever is left is read from where it is listed
    for (const int start : std::as_const(starts)) {
        int at = start;
        while (at != 0 && !out.contains(at)) {
            out.append(at);
            int next = 0;
            for (const BalloonTail& t : b.tails)
                if (t.isBridge() && (t.root == at || t.to == at)) {
                    const int other = t.root == at ? t.to : t.root;
                    if (!out.contains(other) && next == 0)
                        next = other;
                }
            at = next;
        }
    }
    return out;
}

int bridgeBetween(const Balloon& b, int a, int c)
{
    for (const BalloonTail& t : b.tails)
        if (t.isBridge() && ((t.root == a && t.to == c) || (t.root == c && t.to == a)))
            return t.id;
    return 0;
}

ChainError canBridge(const Balloon& b, int from, int to)
{
    if (!hasBody(b, from) || !hasBody(b, to))
        return ChainError::NotABody;
    if (from == to)
        return ChainError::SameBody;
    if (!chainEnd(b, from) || !chainEnd(b, to))
        return ChainError::NotAChainEnd;
    if (joined(b, from, to))
        return ChainError::ClosesRing;
    return ChainError::None;
}

ChainError addBridge(Composition& c, int from, int to)
{
    if (!c.balloon)
        return ChainError::NoBalloon;
    if (const ChainError e = canBridge(*c.balloon, from, to); e != ChainError::None)
        return e;
    BalloonTail bridge;
    bridge.id        = c.nextId++;
    bridge.root      = from;
    bridge.to        = to;
    bridge.tail.look = c.balloon->style.nextLook;
    c.balloon->tails.append(bridge);
    return ChainError::None;
}

ChainError makeBridge(Composition& c, int tail, int to)
{
    if (!c.balloon)
        return ChainError::NoBalloon;
    for (BalloonTail& t : c.balloon->tails) {
        if (t.id != tail || t.isBridge())
            continue;
        if (const ChainError e = canBridge(*c.balloon, t.root, to); e != ChainError::None)
            return e;
        t.to = to;
        return ChainError::None;
    }
    return ChainError::NotATail;
}

ChainError addBody(Composition& c, int from, const QPointF& by, int* added)
{
    if (!c.balloon)
        return ChainError::NoBalloon;
    const Body* source = nullptr;
    for (const Body& x : c.balloon->bodies)
        if (x.id == from)
            source = &x;
    if (!source)
        return ChainError::NotABody;
    if (!chainEnd(*c.balloon, from))
        return ChainError::NotAChainEnd;

    Body body = *source;
    body.id   = c.nextId++;
    body.place.pos += by;
    c.balloon->bodies.append(body);

    Words words;
    words.id   = c.nextId++;
    words.seat = body.id;
    for (const Words& w : c.words)
        if (w.seat == from) {
            words.text = w.text;
            break;
        }
    words.text.body.clear();
    words.text.runs.clear();
    c.words.append(words);

    const ChainError e = addBridge(c, from, body.id);
    Q_ASSERT(e == ChainError::None);   // a new body is a lone chain end
    if (added)
        *added = body.id;
    return e;
}

ChainError merge(Composition& c, const Composition& other, const QPointF& by, QList<int>* bodies)
{
    if (!c.balloon || !other.balloon)
        return ChainError::NoBalloon;
    QHash<int, int> ids;   // other's id → its id in c
    const auto renumber = [&](int id) { return ids.contains(id) ? ids.value(id) : ids[id] = c.nextId++; };

    for (Body body : other.balloon->bodies) {
        body.id = renumber(body.id);
        body.place.pos += by;
        c.balloon->bodies.append(body);
        if (bodies)
            bodies->append(body.id);
    }
    for (BalloonTail t : other.balloon->tails) {
        t.id   = renumber(t.id);
        t.root = renumber(t.root);
        if (t.isBridge())
            t.to = renumber(t.to);
        t.tail.tip += by;
        c.balloon->tails.append(t);
    }
    for (Words w : other.words) {
        w.id = renumber(w.id);
        if (w.seat != 0)
            w.seat = renumber(w.seat);
        else
            w.place.pos += by;
        c.words.append(w);
    }
    return ChainError::None;
}

QList<Composition> pieces(const Composition& c)
{
    if (!c.balloon)
        return {c};
    QList<Composition> out;
    QSet<int>          placed;
    for (const Body& x : c.balloon->bodies) {
        if (placed.contains(x.id))
            continue;
        const QSet<int> chain = reach(*c.balloon, x.id);
        placed.unite(chain);
        out.append(piece(c, chain, out.isEmpty()));
    }
    return out.size() > 1 ? out : QList<Composition>{c};
}

QList<Composition> withoutBridge(const Composition& c, int bridge)
{
    if (!c.balloon)
        return {c};
    Composition cut = c;
    if (!cut.balloon->tails.removeIf([&](const BalloonTail& t) { return t.id == bridge && t.isBridge(); }))
        return {c};
    return pieces(cut);
}

QList<Composition> withoutBody(const Composition& c, int body)
{
    if (!c.balloon || c.balloon->bodies.size() < 2 || !hasBody(*c.balloon, body))
        return {c};
    QSet<int> keep;
    for (const Body& x : c.balloon->bodies)
        if (x.id != body)
            keep.insert(x.id);
    return pieces(piece(c, keep, true));
}

}  // namespace Chain
