# tools/version_header.bzl

def _version_header_impl(ctx):
    status_file = ctx.info_file
    output_file = ctx.outputs.out
    template = ctx.file.template

    # 告訴 Bazel 我們需要解析 status 檔案並進行模板替換
    # ctx.actions.expand_template 支援從 status 檔案（info_file）中提取穩定變數
    ctx.actions.expand_template(
        template = template,
        output = output_file,
        substitutions = {
            # 這是 Bazel 的特殊語法，會自動去 status_file 尋找對應的鍵值並替換
            "{%STABLE_GIT_COMMIT_HASH%}": "$STABLE_GIT_COMMIT_HASH",
        },
    )
    return [DefaultInfo(files = depset([output_file]))]

version_header = rule(
    implementation = _version_header_impl,
    attrs = {
        "template": attr.label(
            allow_single_file = True,
            default = "//tools:git_hash.h.tpl",
        ),
    },
    outputs = {"out": "generated/git_hash.h"},
)
