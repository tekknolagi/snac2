/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_regex.h"

#include "snac.h"

d_char *not_really_markdown(char *content, d_char **f_content)
/* formats a content using some Markdown rules */
{
    d_char *s = NULL;
    int in_pre = 0;
    int in_blq = 0;
    xs *list;
    char *p, *v;
    xs *wrk = xs_str_new(NULL);

    {
        /* split by special markup */
        xs *sm = xs_regex_split(content,
            "(`[^`]+`|\\*\\*?[^\\*]+\\*?\\*|https?:/" "/[^ ]*)");
        int n = 0;

        p = sm;
        while (xs_list_iter(&p, &v)) {
            if ((n & 0x1)) {
                /* markup */
                if (xs_startswith(v, "`")) {
                    xs *s1 = xs_crop(xs_dup(v), 1, -1);
                    xs *s2 = xs_fmt("<code>%s</code>", s1);
                    wrk = xs_str_cat(wrk, s2);
                }
                else
                if (xs_startswith(v, "**")) {
                    xs *s1 = xs_crop(xs_dup(v), 2, -2);
                    xs *s2 = xs_fmt("<b>%s</b>", s1);
                    wrk = xs_str_cat(wrk, s2);
                }
                else
                if (xs_startswith(v, "*")) {
                    xs *s1 = xs_crop(xs_dup(v), 1, -1);
                    xs *s2 = xs_fmt("<i>%s</i>", s1);
                    wrk = xs_str_cat(wrk, s2);
                }
                else
                if (xs_startswith(v, "http")) {
                    xs *s1 = xs_fmt("<a href=\"%s\">%s</a>", v, v);
                    wrk = xs_str_cat(wrk, s1);
                }
                else
                    /* what the hell is this */
                    wrk = xs_str_cat(wrk, v);
            }
            else
                /* surrounded text, copy directly */
                wrk = xs_str_cat(wrk, v);

            n++;
        }
    }

    /* now work by lines */
    p = list = xs_split(wrk, "\n");

    s = xs_str_new(NULL);

    while (xs_list_iter(&p, &v)) {
        xs *ss = xs_strip(xs_dup(v));

        if (xs_startswith(ss, "```")) {
            if (!in_pre)
                s = xs_str_cat(s, "<pre>");
            else
                s = xs_str_cat(s, "</pre>");

            in_pre = !in_pre;
            continue;
        }

        if (xs_startswith(ss, ">")) {
            /* delete the > and subsequent spaces */
            ss = xs_strip(xs_crop(ss, 1, 0));

            if (!in_blq) {
                s = xs_str_cat(s, "<blockquote>");
                in_blq = 1;
            }

            s = xs_str_cat(s, ss);
            s = xs_str_cat(s, "<br>");

            continue;
        }

        if (in_blq) {
            s = xs_str_cat(s, "</blockquote>");
            in_blq = 0;
        }

        s = xs_str_cat(s, ss);
        s = xs_str_cat(s, "<br>");
    }

    if (in_blq)
        s = xs_str_cat(s, "</blockquote>");
    if (in_pre)
        s = xs_str_cat(s, "</pre>");

    /* some beauty fixes */
    s = xs_replace_i(s, "</blockquote><br>", "</blockquote>");

    *f_content = s;

    return *f_content;
}


int login(snac *snac, char *headers)
/* tries a login */
{
    int logged_in = 0;
    char *auth = xs_dict_get(headers, "authorization");

    if (auth && xs_startswith(auth, "Basic ")) {
        int sz;
        xs *s1 = xs_crop(xs_dup(auth), 6, 0);
        xs *s2 = xs_base64_dec(s1, &sz);
        xs *l1 = xs_split_n(s2, ":", 1);

        if (xs_list_len(l1) == 2) {
            logged_in = check_password(
                xs_list_get(l1, 0), xs_list_get(l1, 1),
                xs_dict_get(snac->config, "passwd"));
        }
    }

    return logged_in;
}


d_char *html_msg_icon(snac *snac, d_char *s, char *msg)
{
    char *actor_id;
    xs *actor = NULL;

    if ((actor_id = xs_dict_get(msg, "attributedTo")) == NULL)
        actor_id = xs_dict_get(msg, "actor");

    if (actor_id && valid_status(actor_get(snac, actor_id, &actor))) {
        xs *name   = NULL;
        xs *avatar = NULL;
        char *v;

        /* get the name */
        if ((v = xs_dict_get(actor, "name")) == NULL) {
            if ((v = xs_dict_get(actor, "preferredUsername")) == NULL) {
                v = "user";
            }
        }

        name = xs_dup(v);

        /* get the avatar */
        if ((v = xs_dict_get(actor, "icon")) != NULL &&
            (v = xs_dict_get(v, "url")) != NULL) {
            avatar = xs_dup(v);
        }

        if (avatar == NULL)
            avatar = xs_fmt("data:image/png;base64, %s", susie);

        {
            xs *s1 = xs_fmt("<p><img class=\"snac-avatar\" src=\"%s\"/>\n", avatar);
            s = xs_str_cat(s, s1);
        }

        {
            xs *s1 = xs_fmt("<a href=\"%s\" class=\"p-author h-card snac-author\">%s</a>",
                actor_id, name);
            s = xs_str_cat(s, s1);
        }

        if (strcmp(xs_dict_get(msg, "type"), "Note") == 0) {
            xs *s1 = xs_fmt(" <a href=\"%s\">»</a>", xs_dict_get(msg, "id"));
            s = xs_str_cat(s, s1);
        }

        if (!is_msg_public(snac, msg))
            s = xs_str_cat(s, " <span title=\"private\">&#128274;</span>");

        if ((v = xs_dict_get(msg, "published")) == NULL)
            v = "&nbsp;";

        {
            xs *s1 = xs_fmt("<br>\n<time class=\"dt-published snac-pubdate\">%s</time>\n", v);
            s = xs_str_cat(s, s1);
        }

        s = xs_str_cat(s, "</div>\n");
    }

    return s;
}


d_char *html_user_header(snac *snac, d_char *s, int local)
/* creates the HTML header */
{
    char *p, *v;

    s = xs_str_cat(s, "<!DOCTYPE html>\n<html>\n<head>\n");
    s = xs_str_cat(s, "<meta name=\"viewport\" "
                      "content=\"width=device-width, initial-scale=1\"/>\n");
    s = xs_str_cat(s, "<meta name=\"generator\" "
                      "content=\"" USER_AGENT "\"/>\n");

    /* add server CSS */
    p = xs_dict_get(srv_config, "cssurls");
    while (xs_list_iter(&p, &v)) {
        xs *s1 = xs_fmt("<link rel=\"stylesheet\" type=\"text/css\" href=\"%s\"/>\n", v);
        s = xs_str_cat(s, s1);
    }

    /* add the user CSS */
    {
        xs *css = NULL;
        int size;

        if (valid_status(static_get(snac, "style.css", &css, &size))) {
            xs *s1 = xs_fmt("<style>%s</style>\n", css);
            s = xs_str_cat(s, s1);
        }
    }

    {
        xs *s1 = xs_fmt("<title>%s</title>\n", xs_dict_get(snac->config, "name"));
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</head>\n<body>\n");

    /* top nav */
    s = xs_str_cat(s, "<nav style=\"snac-top-nav\">");

    {
        xs *s1;

        if (local)
            s1 = xs_fmt("<a href=\"%s/admin\">%s</a></nav>", snac->actor, L("admin"));
        else
            s1 = xs_fmt("<a href=\"%s\">%s</a></nav>", snac->actor, L("public"));

        s = xs_str_cat(s, s1);
    }

    /* user info */
    {
        s = xs_str_cat(s, "<div class=\"h-card snac-top-user\">\n");

        xs *s1 = xs_fmt("<p class=\"p-name snac-top-user-name\">%s</p>\n",
            xs_dict_get(snac->config, "name"));
        s = xs_str_cat(s, s1);

        xs *s2 = xs_fmt("<p class=\"snac-top-user-id\">@%s@%s</p>\n",
            xs_dict_get(snac->config, "uid"), xs_dict_get(srv_config, "host"));
        s = xs_str_cat(s, s2);

        xs *bio = NULL;
        not_really_markdown(xs_dict_get(snac->config, "bio"), &bio);
        xs *s3 = xs_fmt("<div class=\"p-note snac-top-user-bio\">%s</div>\n", bio);
        s = xs_str_cat(s, s3);

        s = xs_str_cat(s, "</div>\n");
    }

    return s;
}


d_char *html_timeline(snac *snac, char *list, int local)
/* returns the HTML for the timeline */
{
    d_char *s = xs_str_new(NULL);

    s = html_user_header(snac, s, local);

    s = xs_str_cat(s, "<h1>HI</h1>\n");

    s = xs_str_cat(s, xs_fmt("len() == %d\n", xs_list_len(list)));

    {
        char *i = xs_list_get(list, 0);
        xs *msg = timeline_get(snac, i);

        s = html_msg_icon(snac, s, msg);
    }

    s = xs_str_cat(s, "</html>\n");

    return s;
}


int html_get_handler(d_char *req, char *q_path, char **body, int *b_size, char **ctype)
{
    int status = 404;
    snac snac;
    char *uid, *p_path;

    xs *l = xs_split_n(q_path, "/", 2);

    uid = xs_list_get(l, 1);
    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_log(xs_fmt("html_get_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    if (p_path == NULL) {
        /* public timeline */
        xs *list = local_list(&snac, 0xfffffff);

        *body   = html_timeline(&snac, list, 1);
        *b_size = strlen(*body);
        status  = 200;
    }
    else
    if (strcmp(p_path, "admin") == 0) {
        /* private timeline */

        if (!login(&snac, req))
            status = 401;
        else {
            xs *list = timeline_list(&snac, 0xfffffff);

            *body   = html_timeline(&snac, list, 0);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (xs_startswith(p_path, "p/") == 0) {
        /* a timeline with just one entry */
    }
    else
    if (xs_startswith(p_path, "s/") == 0) {
        /* a static file */
    }
    else
    if (xs_startswith(p_path, "h/") == 0) {
        /* an entry from the history */
    }
    else
        status = 404;

    user_free(&snac);

    if (valid_status(status)) {
        *ctype = "text/html; charset=utf-8";
    }

    return status;
}


int html_post_handler(d_char *req, char *q_path, d_char *payload, int p_size,
                      char **body, int *b_size, char **ctype)
{
    int status = 0;

    return status;
}

