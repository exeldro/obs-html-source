#include "html-source.h"

#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QPainter>

void render_qt(struct html_source_data *hs, obs_data_t *settings)
{

	const char *new_html = obs_data_get_string(settings, "text");
	if (hs->html.array && strcmp(hs->html.array, new_html) == 0)
		return;
	dstr_copy(&hs->html, new_html);

	QTextDocument td;
	long long source_type = obs_data_get_int(settings, "html_source");
	if (source_type == HTML_FILE) {
		auto url = QUrl::fromLocalFile(QString::fromUtf8(obs_data_get_string(settings, "html_path"))).toString();
		td.setMetaInformation(QTextDocument::DocumentUrl, url);
	} else if (source_type == HTML_WEB) {
		auto url = QString::fromUtf8(obs_data_get_string(settings, "html_url"));
		td.setMetaInformation(QTextDocument::DocumentUrl, url);
	}
	auto f = td.defaultFont();
	obs_data_t *font = obs_data_get_obj(settings, "font");
	if (font) {
		auto face = obs_data_get_string(font, "face");
		if (face && face[0] != '\0')
			f.setFamily(face);
		auto style = obs_data_get_string(font, "style");
		if (style && style[0] != '\0')
			f.setStyleName(style);

		auto fs = obs_data_get_int(font, "size");
		if (fs > 0)
			f.setPointSize(fs);
		else
			f.setPointSize(32);
		uint32_t flags = (uint32_t)obs_data_get_int(font, "flags");
		if (flags & OBS_FONT_BOLD)
			f.setBold(true);
		if (flags & OBS_FONT_ITALIC)
			f.setItalic(true);
		if (flags & OBS_FONT_UNDERLINE)
			f.setUnderline(true);
		if (flags & OBS_FONT_STRIKEOUT)
			f.setStrikeOut(true);
	} else {
		f.setPointSize(32);
	}
	td.setDefaultFont(f);
	td.setHtml(QString::fromUtf8(hs->html.array));
	auto size = td.documentLayout()->documentSize();
	if (!hs->texture || size != QSizeF(hs->cx, hs->cy)) {
		obs_enter_graphics();
		hs->cx = (uint32_t)size.width();
		hs->cy = (uint32_t)size.height();
		if (hs->texture)
			gs_texture_destroy(hs->texture);
		hs->texture = gs_texture_create(hs->cx, hs->cy, GS_RGBA, 1, NULL, GS_DYNAMIC);
		obs_leave_graphics();
	}
	QImage image(hs->cx, hs->cy, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	td.drawContents(&painter);
	painter.end();
	obs_enter_graphics();
	gs_texture_set_image(hs->texture, image.constBits(), image.bytesPerLine(), false);
	obs_leave_graphics();
}
