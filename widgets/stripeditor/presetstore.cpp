#include "presetstore.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSettings>

namespace StripEdit {

namespace {

const auto k_presetsKey = QStringLiteral("bubblePresets");
//! Marks a file as a pack rather than any other JSON array that happens to parse.
const auto k_packMarker = QStringLiteral("platemakerBubblePresets");

/**
 * @brief A preset is an artifact with its content removed.
 *
 * Dropping the keys rather than listing the ones to keep is what makes this stay correct: a styling
 * field added to TextArtifact is carried by artifactToJson() and lands in presets for free, while a new
 * *content* field is the only thing that needs a line here.
 */
QJsonObject presetToJson(const BubblePreset& p)
{
    QJsonObject j = artifactToJson(p.artifact);
    for (const QString& key : {QStringLiteral("text"), QStringLiteral("w"), QStringLiteral("h"),
                               QStringLiteral("tails"), QStringLiteral("styleSeed")})
        j.remove(key);
    j.insert(QStringLiteral("name"), p.name);
    return j;
}

//! The absent content keys fall back to TextArtifact's own defaults, which is exactly what is wanted.
BubblePreset presetFromJson(const QJsonObject& j)
{
    return {j.value(QStringLiteral("name")).toString(), artifactFromJson(j)};
}

QJsonArray presetsToArray(const QList<BubblePreset>& presets, int from)
{
    QJsonArray arr;
    for (int i = from; i < presets.size(); ++i)
        arr.append(presetToJson(presets.at(i)));
    return arr;
}

QList<BubblePreset> presetsFromArray(const QJsonArray& arr)
{
    QList<BubblePreset> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        BubblePreset p = presetFromJson(v.toObject());
        if (!p.name.isEmpty())
            out.append(std::move(p));   // an unnamed preset has nothing to pick it by
    }
    return out;
}

/**
 * @brief The looks a lettering session starts from, as code rather than as a shipped file.
 *
 * They are parametric — a shape name and a handful of numbers — so there is nothing to install, nothing
 * to find at runtime and nothing to lose. They are also not deletable: "restore defaults" is a feature
 * that does not need writing if the defaults were never removable in the first place.
 */
QList<BubblePreset> builtinPresets()
{
    QList<BubblePreset> out;

    TextArtifact dialogue;                       // the struct's own defaults are already a speech balloon
    out.append({PresetStore::tr("Dialogue"), dialogue});

    TextArtifact whisper = dialogue;
    whisper.shape.kind       = TextArtifact::Shape::Ellipse;
    whisper.skin.strokeWidth = 3;
    whisper.skin.stroke      = QColor(90, 90, 90);
    whisper.text.colour      = QColor(70, 70, 70);
    whisper.text.pixelSize   = 26;
    out.append({PresetStore::tr("Whisper"), whisper});

    TextArtifact thought = dialogue;
    thought.shape.kind       = TextArtifact::Shape::Thought;
    thought.skin.strokeWidth = 4;
    out.append({PresetStore::tr("Thought"), thought});

    TextArtifact shout = dialogue;
    shout.shape.kind         = TextArtifact::Shape::Shout;
    shout.text.bold          = true;
    shout.text.pixelSize     = 38;
    shout.skin.strokeWidth   = 7;
    shout.style.kind         = TextArtifact::Style::Marker;
    out.append({PresetStore::tr("Shout"), shout});

    TextArtifact caption = dialogue;
    caption.shape.kind       = TextArtifact::Shape::Caption;
    caption.skin.fill        = QColor(16, 16, 16);
    caption.text.colour      = QColor(245, 245, 245);
    caption.skin.stroke      = QColor(245, 245, 245);
    caption.skin.strokeWidth = 2;
    caption.text.align       = Qt::AlignLeft;
    out.append({PresetStore::tr("Caption"), caption});

    return out;
}

} // namespace

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

int PresetStore::save(const QString& name, const TextArtifact& look, bool replaceExisting,
                      int* existingIndex)
{
    BubblePreset p{name, look};
    p.artifact.tails.items.clear();
    p.artifact.styleSeed = 0;   // content, not style: a stored seed would clone one bubble's wobble

    for (int i = m_builtinCount; i < m_presets.size(); ++i) {
        if (m_presets.at(i).name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        if (existingIndex)
            *existingIndex = i;
        if (!replaceExisting)
            return -1;
        m_presets[i] = p;
        persist();
        return i;
    }

    m_presets.append(p);
    persist();
    return int(m_presets.size()) - 1;
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
        if (at >= 0)
            m_presets[at] = p;
        else
            m_presets.append(p);
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

int PresetStore::matching(const TextArtifact& a) const
{
    for (int i = 0; i < m_presets.size(); ++i) {
        const TextArtifact look = applied(m_presets.at(i), a, /*keepShape=*/false);
        if (look.shape == a.shape && look.skin == a.skin && look.style == a.style
            && look.text == a.text)
            return i;
    }
    return -1;
}

QString PresetStore::lookLabel(const TextArtifact& a) const
{
    const int i = matching(a);
    return i < 0 ? tr("Custom") : m_presets.at(i).name;
}

TextArtifact PresetStore::applied(const BubblePreset& p, const TextArtifact& target, bool keepShape)
{
    TextArtifact a = p.artifact;

    // A preset is a look, not a line: whatever the bubble says, how big it is and where its tails point
    // survive being restyled. Without this, picking a preset would erase the lettering.
    a.text.body   = target.text.body;
    a.box         = target.box;
    a.tails.items = target.tails.items;

    if (keepShape)
        a.shape.kind = target.shape.kind;
    if (a.shape.kind == TextArtifact::Shape::None)
        a.tails.items.clear();

    // Keep the object's own seed. Re-rolling it would make an already-placed marker outline jump for a
    // reason the author did not ask for.
    a.styleSeed = target.styleSeed;
    if (a.style.kind != TextArtifact::Style::Clean && a.styleSeed == 0)
        a.styleSeed = QRandomGenerator::global()->generate();

    return a;
}

}  // namespace StripEdit
