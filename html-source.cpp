#include "html-source.h"

#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QPainter>

void render_qt(struct html_source_data *hs, obs_data_t *settings)
{
	QTextDocument td;
	td.setResourceProvider([hs, settings](const QUrl &url) -> QVariant {
		if (!hs->curl)
			return QVariant();

		std::string url_str;
		if (url.isRelative()) {
			auto base_url = QUrl(QString::fromUtf8(obs_data_get_string(settings, "html_url")));
			url_str = base_url.resolved(url).toString().toStdString();
		} else {
			url_str = url.toString().toStdString();
		}
		da_resize(hs->web_data, 0);
		curl_easy_setopt(hs->curl, CURLOPT_URL, url_str.c_str());
		CURLcode code = curl_easy_perform(hs->curl);
		if (code != CURLE_OK)
			return QVariant();

		long response_code;
		if (curl_easy_getinfo(hs->curl, CURLINFO_RESPONSE_CODE, &response_code) != CURLE_OK)
			return QVariant();
		if (response_code >= 400)
			return QVariant();

		char *content_type = nullptr;
		if (curl_easy_getinfo(hs->curl, CURLINFO_CONTENT_TYPE, &content_type) != CURLE_OK || !content_type)
			return QVariant();

		if (strncmp(content_type, "image/", 6) == 0) {
			QByteArray data((const char *)hs->web_data.array, hs->web_data.num);
			QImage image;
			if (!image.loadFromData(data))
				return QVariant();
			return image;
		} else if (strncmp(content_type, "text/", 5) == 0) {
			uint8_t null_terminator = 0;
			da_push_back(hs->web_data, &null_terminator);
			return QString::fromUtf8((const char *)hs->web_data.array, hs->web_data.num);
		} else {
			return QVariant();
		}

		return QVariant();
	});
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
		obs_data_release(font);
	} else {
		f.setPointSize(32);
	}
	td.setDefaultFont(f);
	const char *html = obs_data_get_string(settings, "text");
	td.setHtml(QString::fromUtf8(html));

	QSizeF size;
	if (obs_data_get_bool(settings, "fixed_size")) {
		int width = obs_data_get_int(settings, "width");
		int height = obs_data_get_int(settings, "height");
		size = QSizeF(width, height);
		td.setPageSize(size);
	} else {
		size = td.documentLayout()->documentSize();
	}

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
