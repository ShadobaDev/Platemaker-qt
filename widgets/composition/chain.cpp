#include "chain.hpp"

#include <QHash>
#include <QLineF>
#include <QSet>

#include <algorithm>
#include <limits>

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

void moveBody(Composition& c, int body, const QPointF& by)
{
    if (!c.balloon)
        return;
    for (Body& b : c.balloon->bodies)
        if (b.id == body)
            b.place.pos += by;
    for (BalloonTail& t : c.balloon->tails)
        if (t.root == body && !t.isBridge())
            t.tail.tip += by;
}

void resizeBody(Composition& c, int body, const QRectF& to)
{
    Body* own = bodyOf(c, body);
    if (!own || body == 0)
        return;
    own->place.pos = to.topLeft();
    own->size      = to.size().toSize();
}

QList<int> wordsInOrder(const Composition& c)
{
    QList<int> out;
    if (c.balloon)
        for (const int body : order(*c.balloon))
            for (const Words& w : c.words)
                if (w.seat == body)
                    out.append(w.id);
    for (const Words& w : c.words)
        if (!out.contains(w.id))
            out.append(w.id);
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

ChainError join(Composition& c, const Composition& other, const QPointF& by)
{
    if (!c.balloon || !other.balloon)
        return ChainError::NoBalloon;
    Composition next = c;
    QList<int>  merged;
    if (const ChainError e = merge(next, other, by, &merged); e != ChainError::None)
        return e;

    const auto middle = [&next](int id) {
        const Body* b = bodyOf(next, id);
        return QRectF(b->place.pos, QSizeF(b->size)).center();
    };
    int   from = 0, to = 0;
    qreal best = std::numeric_limits<qreal>::max();
    for (const Body& own : c.balloon->bodies) {
        if (!chainEnd(*next.balloon, own.id))
            continue;
        for (int theirs : std::as_const(merged)) {
            if (!chainEnd(*next.balloon, theirs))
                continue;
            const qreal apart = QLineF(middle(own.id), middle(theirs)).length();
            if (apart < best) {
                best = apart;
                from = own.id;
                to   = theirs;
            }
        }
    }
    if (from == 0)
        return ChainError::NotAChainEnd;
    if (const ChainError e = addBridge(next, from, to); e != ChainError::None)
        return e;
    c = next;
    return ChainError::None;
}

ChainError canJoinByTail(const Balloon& own, int tail, const Balloon& other, int to)
{
    const auto held = std::find_if(own.tails.cbegin(), own.tails.cend(),
                                   [tail](const BalloonTail& t) { return t.id == tail && !t.isBridge(); });
    if (held == own.tails.cend())
        return ChainError::NotATail;
    if (!hasBody(other, to))
        return ChainError::NotABody;
    // One of the two goes in whole: the held tail's chain before the target, or the target's after the tail's body.
    if (order(own).constLast() != held->root && order(other).constFirst() != to)
        return ChainError::NotAChainEnd;
    return ChainError::None;
}

ChainError joinByTail(Composition& c, const Composition& other, const QPointF& by, int tail, int to)
{
    if (!c.balloon || !other.balloon)
        return ChainError::NoBalloon;
    if (const ChainError e = canJoinByTail(*c.balloon, tail, *other.balloon, to); e != ChainError::None)
        return e;
    Composition next = c;
    QList<int>  merged;
    merge(next, other, by, &merged);
    // Other's ids as merge() gave them: merged lists them in other's body order.
    const auto renumbered = [&](int id) {
        for (qsizetype i = 0; i < other.balloon->bodies.size(); ++i)
            if (other.balloon->bodies.at(i).id == id)
                return merged.at(i);
        return 0;
    };
    const QList<int> mine = order(*c.balloon);
    QList<int>       theirs;
    for (const int id : order(*other.balloon))
        theirs.append(renumbered(id));

    if (mine.size() == 1 && theirs.size() > 1) {   // a lone bubble wears the chain it joins
        next.balloon->style = other.balloon->style;
        next.seed           = other.seed;
        next.preset         = other.preset;
    }

    Balloon&        b      = *next.balloon;
    const int       target = renumbered(to);
    const qsizetype at     = theirs.indexOf(target);
    int             root   = 0;
    for (const BalloonTail& t : b.tails)
        if (t.id == tail)
            root = t.root;

    // A bridge that had @p from at one end gets @p onto there instead, aimed from its middle.
    const auto move = [&b](int bridge, int from, int onto) {
        for (BalloonTail& t : b.tails)
            if (t.id == bridge) {
                if (t.root == from) {
                    t.root        = onto;
                    t.tail.anchor = Tail::k_centre;
                } else {
                    t.to       = onto;
                    t.toAnchor = Tail::k_centre;
                }
            }
    };
    if (mine.constLast() == root) {   // the whole chain before the target
        if (at > 0)
            move(bridgeBetween(b, theirs.at(at - 1), target), target, mine.constFirst());
    } else {                           // the whole other chain after the tail's body (the target is its first)
        const qsizetype own = mine.indexOf(root);
        move(bridgeBetween(b, root, mine.at(own + 1)), root, theirs.constLast());
    }
    for (BalloonTail& t : b.tails)
        if (t.id == tail) {
            t.to       = target;
            t.toAnchor = Tail::k_centre;
        }
    // No body is relisted: the new first is the first end listed (c's first, or other's, ahead of its last), and
    // order() reads from there.
    c = next;
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
