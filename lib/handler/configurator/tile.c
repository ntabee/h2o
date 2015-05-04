/*
 * Copyright (c) 2014 DeNA Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "h2o.h"
#include "h2o/configurator.h"

struct st_h2o_tile_config_vars_t {
    const char* base_path;
    const char* style_file_path;
};

struct st_h2o_tile_configurator_t {
    h2o_configurator_t super;
    struct st_h2o_tile_config_vars_t *vars;
    struct st_h2o_tile_config_vars_t _vars_stack[H2O_CONFIGURATOR_NUM_LEVELS + 1];
};

static int on_config_dir(h2o_configurator_command_t *cmd, h2o_configurator_context_t *ctx, yoml_t *node)
{
    struct st_h2o_tile_configurator_t *self = (void *)cmd->configurator;
    if (self->vars->base_path != NULL) {
        // Only one base path can be mapped.
        h2o_configurator_errprintf(cmd, node, "duplicate tile path %s: another location %s is already specified", node->data.scalar, self->vars->base_path);
        return -1;
    }
    self->vars->base_path = node->data.scalar;
    h2o_tile_register(ctx->pathconf, self->vars->base_path, self->vars->style_file_path);
    return 0;
}
static int on_config_style(h2o_configurator_command_t *cmd, h2o_configurator_context_t *ctx, yoml_t *node)
{
    struct st_h2o_tile_configurator_t *self = (void *)cmd->configurator;
    if (self->vars->style_file_path != NULL) {
        // Only one base path can be mapped.
        h2o_configurator_errprintf(cmd, node, "duplicate style path %s: another file %s is already specified", node->data.scalar, self->vars->style_file_path);
        return -1;
    }
    self->vars->style_file_path = node->data.scalar;

    return 0;
}

static int on_config_enter(h2o_configurator_t *_self, h2o_configurator_context_t *ctx, yoml_t *node)
{
    struct st_h2o_tile_configurator_t *self = (void *)_self;
    ++self->vars;
    self->vars[0].base_path = NULL;
    self->vars[0].style_file_path = NULL;
    return 0;
}

static int on_config_exit(h2o_configurator_t *_self, h2o_configurator_context_t *ctx, yoml_t *node)
{
    struct st_h2o_tile_configurator_t *self = (void *)_self;
    --self->vars;
    return 0;
}
void h2o_tile_register_configurator(h2o_globalconf_t *globalconf)
{
    struct st_h2o_tile_configurator_t *self = (void *)h2o_configurator_create(globalconf, sizeof(*self));

    self->super.enter = on_config_enter;
    self->super.exit = on_config_exit;
    self->vars = self->_vars_stack;

    h2o_configurator_define_command(&self->super, "tile.dir", H2O_CONFIGURATOR_FLAG_PATH | H2O_CONFIGURATOR_FLAG_EXPECT_SCALAR | H2O_CONFIGURATOR_FLAG_DEFERRED,
                                    on_config_dir); /* "directory under which to serve the target path" */
    h2o_configurator_define_command(&self->super, "tile.style", H2O_CONFIGURATOR_FLAG_PATH | H2O_CONFIGURATOR_FLAG_EXPECT_SCALAR,
                                    on_config_style); /* "path to a Mapnik's style file" */
}
