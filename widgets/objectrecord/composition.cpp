#include "composition.hpp"

Composition compositionOf(const ObjectRecord& a)
{
    Composition c;
    c.seed   = a.styleSeed;
    c.preset = a.presetOrigin;

    int seat = 0;
    if (a.hasSilhouette()) {
        Balloon b;
        b.id       = c.nextId++;
        b.line     = a.style;
        b.fill     = a.skin;
        b.texture  = a.texture;
        b.effects  = a.effects;
        b.nextLook = a.tails.nextLook;
        Body body;
        body.id    = c.nextId++;
        body.size  = a.box;
        body.shape = a.shape;
        b.bodies.append(body);
        for (const Tail& t : a.tails.items)
            b.tails.append({c.nextId++, body.id, t});
        seat      = body.id;
        c.balloon = b;
    } else if (a.isArtwork()) {
        Picture p;
        p.id      = c.nextId++;
        p.file    = a.artwork;
        p.size    = a.box;
        c.picture = p;
    }

    Words w;
    w.id   = c.nextId++;
    w.seat = seat;
    if (seat == 0)
        w.box = a.box;
    w.text = a.text;
    c.words.append(w);
    return c;
}

ObjectRecord recordOf(const Composition& c)
{
    ObjectRecord a;
    a.styleSeed    = c.seed;
    a.presetOrigin = c.preset;
    a.shape.kind   = ObjectRecord::Shape::None;
    if (!c.words.isEmpty()) {
        a.text = c.words.first().text;
        a.box  = c.words.first().box;
    }

    if (c.balloon) {
        // A record is one body: a balloon of more cannot be one, and none exists before the chain.
        Q_ASSERT(c.balloon->bodies.size() == 1);
        const Balloon& b = *c.balloon;
        a.style          = b.line;
        a.skin           = b.fill;
        a.texture        = b.texture;
        a.effects        = b.effects;
        a.tails.nextLook = b.nextLook;
        a.shape          = b.bodies.first().shape;
        a.box            = b.bodies.first().size;
        for (const BalloonTail& t : b.tails)
            a.tails.items.append(t.tail);
    } else if (c.picture) {
        a.artwork = c.picture->file;
        a.box     = c.picture->size;
    }
    return a;
}
