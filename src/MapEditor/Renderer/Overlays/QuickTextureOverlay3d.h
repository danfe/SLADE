#pragma once

#include "MCOverlay.h"
#include "MapEditor/Edit/Edit3D.h"

class ItemSelection;
class GLTexture;
class MapEditContext;

class QuickTextureOverlay3d : public MCOverlay
{
public:
	QuickTextureOverlay3d(MapEditContext* editor);
	~QuickTextureOverlay3d() = default;

	void setTexture(const string& name);
	void applyTexture();

	void update(long frametime) override;

	void   draw(int width, int height, float fade = 1.0f) override;
	void   drawTexture(unsigned index, double x, double bottom, double size, float fade);
	double determineSize(double x, int width) const;

	void close(bool cancel = false) override;

	void doSearch();
	void keyDown(const string& key) override;

	static bool ok(const ItemSelection& sel);

private:
	struct QTTex
	{
		GLTexture* texture;
		string     name;
		QTTex(string name) : texture{ nullptr }, name{ name } {}
	};

	vector<QTTex>   textures_;
	unsigned        current_index_ = 0;
	string          search_;
	double          anim_offset_ = 0.;
	MapEditContext* editor_      = nullptr;
	bool            sel_flats_   = true;
	bool            sel_walls_   = true;
};
