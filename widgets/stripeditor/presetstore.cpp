#include "presetstore.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>

namespace StripEdit {

namespace {

//! The QSettings key that carries the artist's own presets, as a JSON array of objects.
const auto k_presetsKey = QStringLiteral("bubblePresets");
//! Marks a file as a pack rather than any other JSON array that happens to parse.
const auto k_packMarker = QStringLiteral("platemakerBubblePresets");

/**
 * \brief A preset as JSON: `{name, uid, balloon: {shape, style}, lettering}`, the parts in the content's own form
 *        (composition.hpp), the balloon left out for a text preset and the lettering's words always empty.
 */
QJsonObject presetToJson(const BubblePreset& p)
{
    TextProperties lettering = p.lettering;
    lettering.body.clear();
    QJsonObject j{{QStringLiteral("name"), p.name}, {QStringLiteral("uid"), p.uid},
                  {QStringLiteral("lettering"), toJson(lettering)}};
    if (p.balloon)
        j.insert(QStringLiteral("balloon"), QJsonObject{{QStringLiteral("shape"), toJson(p.balloon->shape)},
                                                        {QStringLiteral("style"), toJson(p.balloon->style)}});
    return j;
}

//! presetToJson()'s way back; what it leaves out is its default.
BubblePreset presetFromJson(const QJsonObject& j)
{
    BubblePreset p;
    p.name = j.value(QStringLiteral("name")).toString();
    // One with no uid gets one now, kept from then on.
    const QString uid = j.value(QStringLiteral("uid")).toString();
    p.uid             = uid.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : uid;
    p.lettering       = textFromJson(j.value(QStringLiteral("lettering")).toObject());
    if (const QJsonObject b = j.value(QStringLiteral("balloon")).toObject(); !b.isEmpty())
        p.balloon = BalloonLook{shapeFromJson(b.value(QStringLiteral("shape")).toObject()),
                                balloonStyleFromJson(b.value(QStringLiteral("style")).toObject())};
    else
        p.balloon.reset();
    return p;
}

/**
 * \brief Converts a list of presets to a JSON array, starting at \p from.
 * \param presets  The list of presets to convert.
 * \param from     The index to start from in the list.
 * \return A JSON array representing the presets.
 */
QJsonArray presetsToArray(const QList<BubblePreset>& presets, int from)
{
    QJsonArray arr;
    for (int i = from; i < presets.size(); ++i)
        arr.append(presetToJson(presets.at(i)));
    return arr;
}

/**
 * \brief Converts a JSON array to a list of presets.
 * \param arr  The JSON array to convert.
 * \return A list of BubblePreset constructed from the JSON array.
 */
QList<BubblePreset> presetsFromArray(const QJsonArray& arr)
{
    QList<BubblePreset> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        // A preset is its lettering at least: an entry without one is not a preset.
        if (!v.toObject().contains(QStringLiteral("lettering")))
            continue;
        BubblePreset p = presetFromJson(v.toObject());
        if (!p.name.isEmpty())
            out.append(std::move(p));   // an unnamed preset has nothing to pick it by
    }
    return out;
}

/**
 * \brief The looks a lettering session starts from, as code rather than as a shipped file.
 *
 * They are parametric — a shape name and a handful of numbers — so there is nothing to install, nothing
 * to find at runtime and nothing to lose. They are also not deletable: "restore defaults" is a feature
 * that does not need writing if the defaults were never removable in the first place.
 */
QList<BubblePreset> builtinPresets()
{
    QList<BubblePreset> out;
    const auto add = [&out](const QString& name, const ObjectRecord& a, const char* uid) {
        out.append(BubblePreset::of(name, a, QLatin1String(uid)));
    };

    // The default is not a preset: it is what an object is with none picked, `ObjectRecord`'s own values,
    // and it is not listed. The built-ins are made from it.
    const ObjectRecord base;

    ObjectRecord whisper = base;
    whisper.shape.kind       = ObjectRecord::Shape::Ellipse;
    whisper.style.strokeWidth = 3;
    whisper.style.stroke.colour      = QColor(90, 90, 90);
    whisper.text.fill.colour = QColor(70, 70, 70);
    whisper.text.pixelSize   = 26;
    add(PresetStore::tr("Whisper"), whisper, "builtin-whisper");

    ObjectRecord thought = base;
    thought.shape            = Geometry::shapeTemplate("thought").shape;
    thought.style.strokeWidth = 4;
    add(PresetStore::tr("Thought"), thought, "builtin-thought");

    ObjectRecord shout = base;
    shout.shape              = Geometry::shapeTemplate("shout").shape;
    shout.text.bold          = true;
    shout.text.pixelSize     = 38;
    shout.style.strokeWidth   = 7;
    shout.texture.kind      = ObjectRecord::Texture::Rough;   // the Marker look: Rough's own defaults
    add(PresetStore::tr("Shout"), shout, "builtin-shout");

    ObjectRecord flash = base;
    flash.shape.kind         = ObjectRecord::Shape::Ellipse;
    flash.text.bold          = true;
    flash.style.line         = ObjectRecord::Line::Flash;   // its strokes, on Auto, from the struct
    add(PresetStore::tr("Flash"), flash, "builtin-flash");

    // The two looks Rough replaced: a felt-tip's long slow wobble, and ink's finer one that
    // bleeds.
    ObjectRecord marker = base;
    marker.texture.kind     = ObjectRecord::Texture::Rough;
    add(PresetStore::tr("Marker"), marker, "builtin-marker");

    ObjectRecord ink = base;
    ink.texture              = Geometry::inkRough(ink.texture);
    add(PresetStore::tr("Ink"), ink, "builtin-ink");

    ObjectRecord caption = base;
    caption.shape            = Geometry::shapeTemplate("caption").shape;
    caption.skin.fill.colour        = QColor(16, 16, 16);
    caption.text.fill.colour = QColor(245, 245, 245);
    caption.style.stroke.colour      = QColor(245, 245, 245);
    caption.style.strokeWidth = 2;
    caption.text.align       = Qt::AlignLeft;
    add(PresetStore::tr("Caption"), caption, "builtin-caption");

    return out;
}

} // namespace

BubblePreset BubblePreset::of(const QString& name, const ObjectRecord& a, const QString& uid)
{
    BubblePreset p;
    p.name           = name;
    p.uid            = uid;
    p.lettering      = a.text;
    p.lettering.body.clear();
    if (a.hasSilhouette())
        p.balloon = BalloonLook{a.shape, {a.style, a.skin, a.texture, a.effects, a.tails.nextLook}};
    else
        p.balloon.reset();
    return p;
}

ObjectRecord BubblePreset::look() const
{
    ObjectRecord a;
    a.text = lettering;
    if (balloon) {
        a.shape          = balloon->shape;
        a.style          = balloon->style.line;
        a.skin           = balloon->style.fill;
        a.texture        = balloon->style.texture;
        a.effects        = balloon->style.effects;
        a.tails.nextLook = balloon->style.nextLook;
    } else {
        a.shape.kind = ObjectRecord::Shape::None;
    }
    return a;
}

PresetStore::PresetStore(QObject* parent)
    : QObject(parent)
{
    load();
}

bool PresetStore::isCustom(int index) const
{
    return index >= m_builtinCount && index < m_presets.size();
}

void PresetStore::load()
{
    m_presets      = builtinPresets();
    m_builtinCount = int(m_presets.size());

    const QSettings st;
    const QJsonArray arr =
        QJsonDocument::fromJson(st.value(k_presetsKey).toString().toUtf8()).array();
    m_presets += presetsFromArray(arr);
}

void PresetStore::persist()
{
    // Only the artist's own: the built-ins are code, so storing them would freeze today's version of
    // them into every config that has ever run.
    QSettings st;
    st.setValue(k_presetsKey,
                QString::fromUtf8(QJsonDocument(presetsToArray(m_presets, m_builtinCount))
                                      .toJson(QJsonDocument::Compact)));
    emit changed();
}

int PresetStore::save(const QString& name, const ObjectRecord& look, bool replaceExisting,
                      int* existingIndex)
{
    // A look only: no words, tails, seed (a stored one would clone one bubble's wobble) or origin.
    BubblePreset p = BubblePreset::of(name, look, QUuid::createUuid().toString(QUuid::WithoutBraces));

    for (int i = m_builtinCount; i < m_presets.size(); ++i) {
        if (m_presets.at(i).name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        if (existingIndex)
            *existingIndex = i;
        if (!replaceExisting)
            return -1;
        p.uid        = m_presets.at(i).uid;   // the same preset, edited: the objects made from it still name it
        m_presets[i] = p;
        persist();
        return i;
    }

    m_presets.append(p);
    persist();
    return int(m_presets.size()) - 1;
}

bool PresetStore::rename(int index, const QString& name)
{
    const QString n = name.trimmed();
    if (!isCustom(index) || n.isEmpty())
        return false;
    for (int i = m_builtinCount; i < m_presets.size(); ++i)
        if (i != index && m_presets.at(i).name.compare(n, Qt::CaseInsensitive) == 0)
            return false;
    m_presets[index].name = n;
    persist();
    return true;
}

void PresetStore::remove(int index)
{
    if (!isCustom(index))
        return;
    m_presets.removeAt(index);
    persist();
}

int PresetStore::importPack(const QString& path, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return -1;
    };

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(tr("Cannot read %1.").arg(path));

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (!root.contains(k_packMarker))
        return fail(tr("%1 is not a Platemaker preset pack.").arg(QFileInfo(path).fileName()));

    const QList<BubblePreset> incoming = presetsFromArray(root.value(QStringLiteral("presets")).toArray());
    for (const BubblePreset& p : incoming) {
        // Replace by name rather than accumulate: re-importing an updated pack should update it, not
        // leave the artist choosing between two entries wearing the same name.
        int at = -1;
        for (int i = m_builtinCount; i < m_presets.size(); ++i)
            if (m_presets.at(i).name.compare(p.name, Qt::CaseInsensitive) == 0)
                at = i;
        if (at >= 0) {
            BubblePreset same = p;
            same.uid          = m_presets.at(at).uid;   // replaced in place: the objects made from it still name it
            m_presets[at]     = same;
        } else {
            m_presets.append(p);
        }
    }
    persist();
    return int(incoming.size());
}

bool PresetStore::exportPack(const QString& path, QString* error) const
{
    const QJsonObject root{
        {k_packMarker, 1},
        {QStringLiteral("presets"), presetsToArray(m_presets, m_builtinCount)},
    };
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        if (error)
            *error = tr("Cannot write %1.").arg(path);
        return false;
    }
    return true;
}

int PresetStore::indexOf(const QString& uid) const
{
    if (uid.isEmpty())
        return -1;
    for (int i = 0; i < m_presets.size(); ++i)
        if (m_presets.at(i).uid == uid)
            return i;
    return -1;
}

QString PresetStore::originName(const PresetOrigin& origin) const
{
    const int i = indexOf(origin.uid);
    return i >= 0 ? m_presets.at(i).name : origin.name;
}

int PresetStore::restore(const PresetOrigin& origin, const ObjectRecord& look)
{
    if (const int i = indexOf(origin.uid); i >= 0)
        return i;
    const auto taken = [this](const QString& name) {
        for (int i = m_builtinCount; i < m_presets.size(); ++i)
            if (m_presets.at(i).name.compare(name, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    QString name = origin.name.isEmpty() ? tr("Preset") : origin.name;
    for (int n = 2; taken(name); ++n)
        name = tr("%1 (%2)").arg(origin.name).arg(n);
    m_presets.append(BubblePreset::of(name, look, origin.uid));
    persist();
    return int(m_presets.size()) - 1;
}

ObjectRecord PresetStore::applied(const BubblePreset& p, const ObjectRecord& target, bool keepShape)
{
    // A preset is a look, not a line: whatever the bubble says, how big it is and where its tails point
    // survive being restyled, and so does its own seed (re-rolling it would make a placed marker outline
    // jump for a reason the author did not ask for).
    ObjectRecord a = target;
    a.text         = p.lettering;
    a.text.body    = target.text.body;
    if (p.balloon) {
        if (!keepShape)
            a.shape = p.balloon->shape;
        a.style          = p.balloon->style.line;
        a.skin           = p.balloon->style.fill;
        a.texture        = p.balloon->style.texture;
        a.effects        = p.balloon->style.effects;
        a.tails.nextLook = p.balloon->style.nextLook;
    } else if (!keepShape) {
        a.shape.kind = ObjectRecord::Shape::None;   // a text preset makes a text object; its balloon is left unread
    }
    if (!a.hasSilhouette())
        a.tails.items.clear();
    topUpStyleSeed(a);

    // Where the look came from, as a hint; *No preset* (no uid) leaves none.
    a.presetOrigin = p.uid.isEmpty() ? PresetOrigin{} : PresetOrigin{p.uid, p.name};

    return a;
}

}  // namespace StripEdit
