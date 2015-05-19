#include "h2o.h"
#include "h2o/configurator.h"

#if H2O_TILE && (!H2O_TILE_PROXY)
struct st_h2o_tile_config_vars_t {
    const char* base_path;
    const char* style_file_path;
};
#else
struct st_h2o_tile_config_vars_t {
    const char* base_path;
    const char* upstream;
};
#endif

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
#if H2O_TILE && (!H2O_TILE_PROXY)
    h2o_tile_register(ctx->pathconf, self->vars->base_path, self->vars->style_file_path);
#else
    h2o_tile_proxy_register(ctx->pathconf, self->vars->base_path, self->vars->upstream);
#endif
    return 0;
}

#if H2O_TILE && (!H2O_TILE_PROXY)
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
#else
static int on_config_upstream(h2o_configurator_command_t *cmd, h2o_configurator_context_t *ctx, yoml_t *node)
{
//    self->vars->style_file_path = node->data.scalar;
    struct st_h2o_tile_configurator_t *self = (void *)cmd->configurator;
    if (self->vars->upstream != NULL) {
        // Only one base path can be mapped.
        h2o_configurator_errprintf(cmd, node, "duplicate upstream %s: another host %s is already specified", node->data.scalar, self->vars->upstream);
        return -1;
    }
    self->vars->upstream = node->data.scalar;

    return 0;
}
#endif


static int on_config_enter(h2o_configurator_t *_self, h2o_configurator_context_t *ctx, yoml_t *node)
{
    struct st_h2o_tile_configurator_t *self = (void *)_self;
    self->vars[0].base_path = NULL;
#if H2O_TILE && (!H2O_TILE_PROXY)
    self->vars[0].style_file_path = NULL;
#else
    self->vars[0].upstream = NULL;
#endif
    ++self->vars;
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
    self->vars->base_path = NULL;
#if H2O_TILE && (!H2O_TILE_PROXY)
    self->vars[0].style_file_path = NULL;
#else
    self->vars->upstream = NULL;
#endif
    h2o_configurator_define_command(&self->super, "tile.dir", H2O_CONFIGURATOR_FLAG_PATH | H2O_CONFIGURATOR_FLAG_EXPECT_SCALAR | H2O_CONFIGURATOR_FLAG_DEFERRED,
                                    on_config_dir); /* "directory under which to serve the target path" */
#if H2O_TILE && (!H2O_TILE_PROXY)
    h2o_configurator_define_command(&self->super, "tile.style", H2O_CONFIGURATOR_FLAG_PATH | H2O_CONFIGURATOR_FLAG_EXPECT_SCALAR,
                                    on_config_style); /* "path to a Mapnik's style file" */
#else
    h2o_configurator_define_command(&self->super, "tile.upstream", H2O_CONFIGURATOR_FLAG_PATH | H2O_CONFIGURATOR_FLAG_EXPECT_SCALAR,
                                    on_config_upstream); /* "path to a Mapnik's style file" */
#endif
}
