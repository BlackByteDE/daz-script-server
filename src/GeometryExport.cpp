// Fork-only (BlackByteDE): POST /geometry/frames — see GeometryExport.h.
#include "GeometryExport.h"
#include "JsonStd.h"
#include "ErrorResponse.h"

#include <cstring>
#include <vector>

#include <dzscene.h>
#include <dzskeleton.h>
#include <dzbone.h>
#include <dzobject.h>
#include <dzshape.h>
#include <dzfacetmesh.h>
#include <dzfacegroup.h>
#include <dzface.h>
#include <dzintproperty.h>
#include <dzfloatproperty.h>
#include <dzmorph.h>
#include <dzvec3.h>

#include <QtCore/qvariant.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qmetaobject.h>

namespace {

HttpResult jsonError(ErrorCode code, const QString& detail)
{
	const std::string body = ErrorResponse::build(code, detail.toUtf8().constData());
	return HttpResult(ErrorResponse::httpStatus(code), QByteArray(body.c_str(), (int)body.size()));
}

DzSkeleton* findFigure(const QString& nameOrLabel)
{
	if (!nameOrLabel.isEmpty()) {
		DzNode* node = dzScene->findNode(nameOrLabel);
		if (!node) node = dzScene->findNodeByLabel(nameOrLabel);
		return qobject_cast<DzSkeleton*>(node);
	}
	// Same default as the DazScript tools: first figure that is not fitted to another.
	for (int i = 0; i < dzScene->getNumSkeletons(); ++i) {
		DzSkeleton* skel = dzScene->getSkeleton(i);
		if (skel && !skel->getFollowTarget()) return skel;
	}
	return nullptr;
}

// Channel spec → float property: {"bone": b, "property": p}, {"node": n,
// "property": p}, {"morph": m} or {"property": p} on the figure itself.
// Properties match by name first, then by label.
DzFloatProperty* findChannel(DzSkeleton* fig, const QVariantMap& spec, QString& err)
{
	const QString morph = spec.value("morph").toString();
	if (!morph.isEmpty()) {
		DzObject* obj = fig->getObject();
		if (obj) {
			if (DzMorph* m = qobject_cast<DzMorph*>(obj->findModifier(morph)))
				return m->getValueControl();
			for (int i = 0; i < obj->getNumModifiers(); ++i) {
				DzMorph* m = qobject_cast<DzMorph*>(obj->getModifier(i));
				if (m && m->getValueControl() && m->getValueControl()->getLabel() == morph)
					return m->getValueControl();
			}
		}
		err = "morph not found: " + morph;
		return nullptr;
	}
	DzElement* owner = fig;
	if (spec.contains("bone")) {
		owner = fig->findBone(spec.value("bone").toString());
		if (!owner) { err = "bone not found: " + spec.value("bone").toString(); return nullptr; }
	} else if (spec.contains("node")) {
		const QString name = spec.value("node").toString();
		DzNode* node = dzScene->findNode(name);
		if (!node) node = dzScene->findNodeByLabel(name);
		if (!node) { err = "node not found: " + name; return nullptr; }
		owner = node;
	}
	const QString name = spec.value("property").toString();
	if (name.isEmpty()) { err = "channel needs \"morph\" or \"property\""; return nullptr; }
	DzProperty* prop = owner->findProperty(name);
	if (!prop) prop = owner->findPropertyByLabel(name);
	DzFloatProperty* fp = qobject_cast<DzFloatProperty*>(prop);
	if (!fp) err = QString("float property not found: %1 on %2").arg(name, owner->getLabel());
	return fp;
}

// Sets channel values at one scene time without leaving a trace: an existing
// key at that time gets its value back, a key the override created is deleted,
// an unanimated channel gets its static value back. Restores on destruction.
class ChannelOverrides {
public:
	// mismatches counts channels whose restore could not be verified (key count
	// or value differ) — reported in the v2 header so a client notices.
	ChannelOverrides(const std::vector<DzFloatProperty*>& props, DzTime tm, quint32& mismatches)
		: m_tm(tm), m_mismatches(mismatches)
	{
		m_state.reserve(props.size());
		for (DzFloatProperty* p : props) {
			State s;
			s.prop     = p;
			s.numKeys  = p->getNumKeys();
			int idx    = -1;
			s.keyAtTm  = s.numKeys > 0 && p->isKey(tm, idx);
			s.oldValue = s.keyAtTm ? p->getKeyValue(idx) : p->getLocalValue(tm);
			m_state.push_back(s);
		}
	}
	~ChannelOverrides()
	{
		for (const State& s : m_state) {
			int idx = -1;
			if (s.numKeys == 0) {
				// Without keys a channel sits at its default and any setValue()
				// adds a key (Studio 6), so dropping the keys is the restore.
				s.prop->deleteAllKeys();
			} else if (s.keyAtTm) {
				if (s.prop->isKey(m_tm, idx)) s.prop->setKeyValue(idx, (float)s.oldValue);
			} else if (s.prop->isKey(m_tm, idx)) {
				s.prop->deleteKeys(idx, idx);
			}
			if (s.prop->getNumKeys() != s.numKeys || s.prop->getLocalValue(m_tm) != s.oldValue)
				++m_mismatches;
		}
	}
	void apply(const std::vector<float>& values)
	{
		for (size_t i = 0; i < m_state.size(); ++i) {
			DzFloatProperty* p = m_state[i].prop;
			int idx = -1;
			if (m_state[i].numKeys == 0)  p->setValue(values[i]);
			else if (p->isKey(m_tm, idx)) p->setKeyValue(idx, values[i]);
			else                          p->setValue(m_tm, values[i]);
		}
	}
private:
	struct State {
		DzFloatProperty* prop;
		int              numKeys;
		bool             keyAtTm;
		double           oldValue;
	};
	DzTime             m_tm;
	quint32&           m_mismatches;
	std::vector<State> m_state;
};

// Restores frame and resolution level however exportFrames() leaves.
struct SceneStateGuard {
	int            frame;
	DzIntProperty* resolution;
	int            resolutionValue;
	bool           sceneUpdate;
	~SceneStateGuard() {
		if (resolution) resolution->setValue(resolutionValue);
		dzScene->setFrame(frame);
		if (sceneUpdate) dzScene->update();
	}
};

template <typename T>
void append(QByteArray& out, const T& value)
{
	out.append(reinterpret_cast<const char*>(&value), (int)sizeof(T));
}

} // namespace

HttpResult GeometryExporter::exportFrames(const QByteArray& jsonBody)
{
	QVariantMap body;
	std::string parseErr;
	if (!JsonStd::parseObject(jsonBody, body, parseErr))
		return jsonError(ErrorCode::INVALID_JSON, QString::fromUtf8(parseErr.c_str()));
	if (!dzScene)
		return jsonError(ErrorCode::INTERNAL_ERROR, "dzScene is null");

	// ── Parse request ────────────────────────────────────────────────────────
	const QVariantList frameList = body.value("frames").toList();
	if (frameList.isEmpty())
		return jsonError(ErrorCode::MISSING_FIELD, "frames");
	std::vector<qint32> frames;
	frames.reserve(frameList.size());
	for (const QVariant& f : frameList) frames.push_back((qint32)f.toInt());

	const QString resolutionMode = body.value("resolution", "base").toString();
	if (resolutionMode != "base" && resolutionMode != "current")
		return jsonError(ErrorCode::INVALID_FIELD, "resolution must be \"base\" or \"current\"");

	DzSkeleton* fig = findFigure(body.value("node").toString());
	if (!fig)
		return jsonError(ErrorCode::INVALID_FIELD, "figure not found: " + body.value("node").toString());
	DzObject* obj = fig->getObject();
	DzShape*  shape = obj ? obj->getCurrentShape() : nullptr;
	if (!shape)
		return jsonError(ErrorCode::INVALID_FIELD, "figure has no geometry: " + fig->getLabel());

	std::vector<DzBone*> bones;
	for (const QVariant& b : body.value("bones").toList()) {
		DzBone* bone = fig->findBone(b.toString());
		if (!bone) return jsonError(ErrorCode::INVALID_FIELD, "bone not found: " + b.toString());
		bones.push_back(bone);
	}

	// Override sets: one row of values per set, one value per channel.
	std::vector<DzFloatProperty*> channels;
	for (const QVariant& c : body.value("channels").toList()) {
		QString err;
		DzFloatProperty* p = findChannel(fig, c.toMap(), err);
		if (!p) return jsonError(ErrorCode::INVALID_FIELD, err);
		channels.push_back(p);
	}
	const QVariantList valueRows = body.value("values").toList();
	if (channels.empty() != valueRows.isEmpty())
		return jsonError(ErrorCode::INVALID_FIELD, "\"channels\" and \"values\" must be given together");
	std::vector<std::vector<float>> sets;
	for (const QVariant& r : valueRows) {
		const QVariantList row = r.toList();
		if (row.size() != (int)channels.size())
			return jsonError(ErrorCode::INVALID_FIELD,
				QString("values row %1 has %2 entries, expected %3").arg(sets.size()).arg(row.size()).arg(channels.size()));
		std::vector<float> vals;
		vals.reserve(row.size());
		for (const QVariant& v : row) {
			bool ok = false;
			vals.push_back((float)v.toDouble(&ok));
			if (!ok) return jsonError(ErrorCode::INVALID_FIELD, QString("values row %1: not a number").arg(sets.size()));
		}
		sets.push_back(vals);
	}
	const bool hasSets = !sets.empty();
	const size_t evalsPerFrame = hasSets ? sets.size() : 1;

	// Bones only: skips the mesh deformation — getWSPos() evaluates the joint
	// chain on its own, so pure bone IKs avoid ~0.2 s of forceCacheUpdate per set.
	const bool withVerts = body.value("vertices", true).toBool();
	if (!withVerts && bones.empty())
		return jsonError(ErrorCode::INVALID_FIELD, "\"vertices\": false needs \"bones\"");

	// Vertex subset: explicit indices first, then the surfaces' facet vertices,
	// each vertex once, in first-seen order.
	DzFacetMesh* baseMesh = qobject_cast<DzFacetMesh*>(shape->getGeometry());
	const int baseVerts = baseMesh ? baseMesh->getNumVertices() : 0;
	std::vector<quint32> subset;
	const QVariantList indexList   = body.value("indices").toList();
	const QStringList  surfaceList = body.value("surfaces").toStringList();
	const bool hasSubset = !indexList.isEmpty() || !surfaceList.isEmpty();
	if (hasSubset && !withVerts)
		return jsonError(ErrorCode::INVALID_FIELD, "\"indices\"/\"surfaces\" need vertices");
	if (hasSubset) {
		if (!baseMesh)
			return jsonError(ErrorCode::INTERNAL_ERROR, "figure geometry is not a facet mesh");
		std::vector<char> seen(baseVerts, 0);
		auto add = [&](int idx) {
			if (idx >= 0 && idx < baseVerts && !seen[idx]) { seen[idx] = 1; subset.push_back((quint32)idx); }
		};
		for (const QVariant& v : indexList) {
			const int idx = v.toInt();
			if (idx < 0 || idx >= baseVerts)
				return jsonError(ErrorCode::INVALID_FIELD, QString("vertex index out of range: %1").arg(idx));
			add(idx);
		}
		const DzFacet* facets = baseMesh->getFacetsPtr();
		for (const QString& name : surfaceList) {
			DzMaterialFaceGroup* grp = baseMesh->findMaterialGroup(name);
			if (!grp) return jsonError(ErrorCode::INVALID_FIELD, "surface not found: " + name);
			for (int i = 0; i < grp->count(); ++i) {
				const DzFacet& f = facets[grp->getIndex(i)];
				add(f.getVert1()); add(f.getVert2()); add(f.getVert3());
				if (f.getVert4() >= 0) add(f.getVert4());
			}
		}
	}

	// ── Evaluate frames ──────────────────────────────────────────────────────
	SceneStateGuard guard;
	guard.frame           = dzScene->getFrame();
	guard.resolution      = nullptr;
	guard.resolutionValue = 0;
	guard.sceneUpdate     = body.value("sceneUpdate", true).toBool();
	if (withVerts && resolutionMode == "base" && baseMesh) {
		// The cached geometry is usually the base cage even at "High Resolution"
		// (SubD is applied later), and toggling the level costs ~1 s of
		// re-evaluation — so only switch when the vertex count says we must.
		dzScene->setFrame(frames[0]);
		obj->forceCacheUpdate(fig);
		DzVertexMesh* probe = obj->getCachedGeom();
		if (!probe || probe->getNumVertices() != baseVerts) {
			DzIntProperty* res = qobject_cast<DzIntProperty*>(shape->findPropertyByLabel("Resolution Level"));
			if (res) {
				guard.resolution      = res;
				guard.resolutionValue = res->getValue();
				res->setValue(0);
			}
		}
	}

	quint32 numVerts = 0;
	int     frameVerts = 0;
	quint32 restoreMismatches = 0;
	QByteArray vertData, boneData;
	for (size_t k = 0; k < frames.size(); ++k) {
		dzScene->setFrame(frames[k]);
		// Restores this frame's channels before the next setFrame (or on error).
		ChannelOverrides overrides(channels, dzScene->getTime(), restoreMismatches);
		for (size_t s = 0; s < evalsPerFrame; ++s) {
			if (hasSets) overrides.apply(sets[s]);
			if (withVerts) {
				obj->forceCacheUpdate(fig);
				DzVertexMesh* mesh = obj->getCachedGeom();
				if (!mesh)
					return jsonError(ErrorCode::INTERNAL_ERROR, "no cached geometry");
				const int     n   = mesh->getNumVertices();
				const DzPnt3* pts = mesh->getVerticesPtr();
				if (k == 0 && s == 0) {
					if (resolutionMode == "base" && baseMesh && n != baseVerts)
						return jsonError(ErrorCode::INTERNAL_ERROR,
							QString("cached mesh has %1 vertices, base mesh %2").arg(n).arg(baseVerts));
					if (hasSubset && n < baseVerts)
						return jsonError(ErrorCode::INTERNAL_ERROR, "cached mesh smaller than base mesh");
					frameVerts = n;
					numVerts   = hasSubset ? (quint32)subset.size() : (quint32)n;
					vertData.reserve((int)(frames.size() * evalsPerFrame * numVerts * sizeof(DzPnt3)));
				} else if (n != frameVerts) {
					return jsonError(ErrorCode::INTERNAL_ERROR, "vertex count changed between evaluations");
				}
				if (hasSubset) {
					for (quint32 idx : subset)
						vertData.append(reinterpret_cast<const char*>(pts[idx]), (int)sizeof(DzPnt3));
				} else {
					vertData.append(reinterpret_cast<const char*>(pts), n * (int)sizeof(DzPnt3));
				}
			}
			for (DzBone* bone : bones) {
				const DzVec3 p = bone->getWSPos();
				append(boneData, p.m_x); append(boneData, p.m_y); append(boneData, p.m_z);
			}
		}
	}
	if (hasSets && withVerts) {
		// The cache still holds the last override set; re-evaluate the restored
		// state so later readers (and sceneUpdate=false callers) see the scene as it was.
		obj->forceCacheUpdate(fig);
	}

	// ── Assemble payload ─────────────────────────────────────────────────────
	QByteArray out;
	out.reserve(32 + (int)frames.size() * 4 + (int)subset.size() * 4 + vertData.size() + boneData.size());
	out.append("DZGF", 4);
	append(out, (quint32)(hasSets ? 2 : 1));
	append(out, (quint32)frames.size());
	append(out, numVerts);
	append(out, (quint32)bones.size());
	append(out, (quint32)((hasSubset ? 1 : 0) | (hasSets ? 2 : 0)));
	append(out, (quint32)(hasSets ? sets.size() : 0));
	append(out, restoreMismatches);
	out.append(reinterpret_cast<const char*>(frames.data()), (int)(frames.size() * sizeof(qint32)));
	if (hasSubset)
		out.append(reinterpret_cast<const char*>(subset.data()), (int)(subset.size() * sizeof(quint32)));
	out.append(vertData);
	out.append(boneData);
	return HttpResult(200, out);
}

// ─── HTTP handler (worker thread) ────────────────────────────────────────────

GeometryFramesHandler::GeometryFramesHandler() : m_pExporter(new GeometryExporter()) {}

GeometryFramesHandler::~GeometryFramesHandler()
{
	m_pExporter->deleteLater();
}

void GeometryFramesHandler::handle(HttpContext& ctx)
{
	QByteArray bodyBytes(ctx.body.c_str(), (int)ctx.body.size());
	HttpResult result;
	QMetaObject::invokeMethod(m_pExporter, "exportFrames",
		Qt::BlockingQueuedConnection,
		Q_RETURN_ARG(HttpResult, result),
		Q_ARG(QByteArray, bodyBytes));
	ctx.respond(result.first, std::string(result.second.constData(), result.second.size()));
	if (result.first == 200)
		ctx.responseContentType = "application/octet-stream";
}

#include "moc_GeometryExport.cpp"
