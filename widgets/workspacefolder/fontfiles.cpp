#include "fontfiles.hpp"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <dwrite.h>

#include <string>
#include <vector>

namespace {

//! Holds one COM interface and releases it — the whole of what ComPtr would do here.
template <class T>
class Com {
public:
    Com() = default;
    ~Com()
    {
        if (m_p)
            m_p->Release();
    }
    Com(const Com&)            = delete;
    Com& operator=(const Com&) = delete;

    T** put() { return &m_p; }
    T*  operator->() const { return m_p; }
    T*  get() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }

private:
    T* m_p = nullptr;
};

//! Whether any of \p names (every language a font carries) is \p family.
bool anyIs(IDWriteLocalizedStrings* names, const QString& family)
{
    for (UINT32 n = 0; names && n < names->GetCount(); ++n) {
        UINT32 length = 0;
        if (FAILED(names->GetStringLength(n, &length)))
            continue;
        std::wstring name(length + 1, L'\0');
        if (SUCCEEDED(names->GetString(n, name.data(), length + 1))) {
            name.resize(length);
            if (QString::fromStdWString(name).compare(family, Qt::CaseInsensitive) == 0)
                return true;
        }
    }
    return false;
}

bool carries(IDWriteFont* font, DWRITE_INFORMATIONAL_STRING_ID id, const QString& family)
{
    Com<IDWriteLocalizedStrings> names;
    BOOL                         exists = FALSE;
    return SUCCEEDED(font->GetInformationalStrings(id, names.put(), &exists)) && exists
        && anyIs(names.get(), family);
}

//! Appends the local files \p font is read from to \p out, each once.
void appendFiles(IDWriteFont* font, QStringList& out)
{
    Com<IDWriteFontFace> face;
    UINT32               count = 0;
    if (FAILED(font->CreateFontFace(face.put())) || FAILED(face->GetFiles(&count, nullptr)))
        return;
    std::vector<IDWriteFontFile*> files(count, nullptr);
    if (FAILED(face->GetFiles(&count, files.data())))
        return;

    for (IDWriteFontFile* file : files) {
        const void*                     key     = nullptr;
        UINT32                          keySize = 0;
        Com<IDWriteFontFileLoader>      loader;
        Com<IDWriteLocalFontFileLoader> local;
        UINT32                          length  = 0;
        if (SUCCEEDED(file->GetReferenceKey(&key, &keySize)) && SUCCEEDED(file->GetLoader(loader.put()))
            && SUCCEEDED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                                reinterpret_cast<void**>(local.put())))
            && SUCCEEDED(local->GetFilePathLengthFromKey(key, keySize, &length))) {
            std::wstring path(length + 1, L'\0');
            if (SUCCEEDED(local->GetFilePathFromKey(key, keySize, path.data(), length + 1))) {
                path.resize(length);
                // DirectWrite hands the path back upper-cased, and canonicalFilePath() keeps that; the
                // folder's own listing has the file's real spelling, which a package entry is named after.
                const QFileInfo   raw(QDir::fromNativeSeparators(QString::fromStdWString(path)));
                const QStringList real = raw.dir().entryList({raw.fileName()}, QDir::Files);
                const QString     p = real.isEmpty() ? raw.filePath() : raw.dir().filePath(real.first());
                if (!out.contains(p, Qt::CaseInsensitive))
                    out << p;
            }
        }
        file->Release();
    }
}

} // namespace

QStringList installedFontFiles(const QString& family)
{
    QStringList out;

    Com<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(factory.put()))))
        return out;
    Com<IDWriteFontCollection> fonts;
    if (FAILED(factory->GetSystemFontCollection(fonts.put(), FALSE)))
        return out;

    // Every font is asked, not FindFamilyName(): that knows only DirectWrite's own family names, and a font
    // is often called something else where Qt looks. Measured on Wieynk Fraktur: five files whose Win32
    // family name — the one Qt lists — is "Wieynk Fraktur", split by DirectWrite into "WieynkFraktur",
    // "WieynkFrakturZier", … none of which FindFamilyName("Wieynk Fraktur") finds. So a font belongs to
    // the family when any of its names says so: its DirectWrite family, its Win32 family, its typographic.
    // ponytail: one pass over every installed font per family asked; batch the families if a workspace
    // ever names enough of them for it to show.
    for (UINT32 f = 0; f < fonts->GetFontFamilyCount(); ++f) {
        Com<IDWriteFontFamily> styles;
        Com<IDWriteLocalizedStrings> familyNames;
        if (FAILED(fonts->GetFontFamily(f, styles.put())) || FAILED(styles->GetFamilyNames(familyNames.put())))
            continue;
        const bool wholeFamily = anyIs(familyNames.get(), family);

        for (UINT32 i = 0; i < styles->GetFontCount(); ++i) {
            Com<IDWriteFont> font;
            if (FAILED(styles->GetFont(i, font.put())) || font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE)
                continue;   // a synthesised face has no file of its own
            if (wholeFamily || carries(font.get(), DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES, family)
                || carries(font.get(), DWRITE_INFORMATIONAL_STRING_PREFERRED_FAMILY_NAMES, family))
                appendFiles(font.get(), out);
        }
    }
    return out;
}

#else

// ponytail: fontconfig (FcFontList on FC_FAMILY → FC_FILE) belongs here; until then every family is
// reported as not packed on Linux, which the export says, and nothing else depends on it.
QStringList installedFontFiles(const QString&)
{
    return {};
}

#endif
