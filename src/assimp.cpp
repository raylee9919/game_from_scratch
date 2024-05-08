/* ―――――――――――――――――――――――――――――――――――◆――――――――――――――――――――――――――――――――――――
   $File: $
   $Date: $
   $Revision: $
   $Creator: Sung Woo Lee $
   $Notice: (C) Copyright 2024 by Sung Woo Lee. All Rights Reserved. $
   ―――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――― */

#include <cstdio>
#include <vector>
#include <unordered_map>
#include <string>


#include "third_party/assimp/include/assimp/Importer.hpp"
#include "third_party/assimp/include/assimp/scene.h"
#include "third_party/assimp/include/assimp/postprocess.h"


#define KB(X) (   X *  1024ll)
#define MB(X) (KB(X) * 1024ll)
#define GB(X) (MB(X) * 1024ll)
#define TB(X) (GB(X) * 1024ll)
#define max(A, B) (A > B ? A : B)
#define min(A, B) (A < B ? A : B)
#define assert(EXP) if (!(EXP)) { *(volatile int *)0 = 0; }

#include "types.h"
#include "assimp.h"
#include "asset_model.h"

static std::unordered_map<std::string, s32> g_bone_map;
static u32 g_bone_map_used;
static std::unordered_map<std::string, u32> g_anim_map;
static u32 g_anim_map_used;
static std::vector<s32> g_bone_hierarchy[MAX_BONE_PER_MESH];
static std::unordered_map<s32, u8> g_bone_visited;
static std::vector<Asset_Animation> g_animations;

static Memory_Arena
init_memory_arena()
{
    size_t total_memory_size = GB(2);
    Memory_Arena arena = {};
    arena.size = total_memory_size;
    arena.used = 0;
    arena.base = malloc(total_memory_size);
    memset(arena.base, 0, total_memory_size);
    return arena;
}

static v3
aiv3_to_v3(aiVector3D ai_v)
{
    v3 v = {};
    v.x = ai_v.x;
    v.y = ai_v.y;
    v.z = ai_v.z;
    return v;
}

static qt
aiqt_to_qt(aiQuaternion ai_q)
{
    qt q = {};
    q.w = ai_q.w;
    q.x = ai_q.x;
    q.y = ai_q.y;
    q.z = ai_q.z;
    return q;
}

static m4x4
ai_m4x4_to_m4x4(aiMatrix4x4 ai_mat)
{
    m4x4 mat = {};
    mat.e[0][0] = ai_mat.a1;
    mat.e[0][1] = ai_mat.a2;
    mat.e[0][2] = ai_mat.a3;
    mat.e[0][3] = ai_mat.a4;

    mat.e[1][0] = ai_mat.b1;
    mat.e[1][1] = ai_mat.b2;
    mat.e[1][2] = ai_mat.b3;
    mat.e[1][3] = ai_mat.b4;

    mat.e[2][0] = ai_mat.c1;
    mat.e[2][1] = ai_mat.c2;
    mat.e[2][2] = ai_mat.c3;
    mat.e[2][3] = ai_mat.c4;

    mat.e[3][0] = ai_mat.d1;
    mat.e[3][1] = ai_mat.d2;
    mat.e[3][2] = ai_mat.d3;
    mat.e[3][3] = ai_mat.d4;
    return mat;
}

static aiNode *
find_root_bone_node(aiNode *node)
{
    aiNode *result = 0;

    std::string name(node->mName.data);
    if (g_bone_map.find(name) != g_bone_map.end())
    {
        result = node;
    }
    else
    {
        for (u32 child_idx = 0;
             child_idx < node->mNumChildren;
             ++child_idx)
        {
            aiNode *child = node->mChildren[child_idx];
            aiNode *tmp = find_root_bone_node(child);
            if (tmp)
            {
                result = tmp;
                break;
            }
        }
    }

    return result;
}

inline void
print_indent(u32 depth)
{
    for (u32 i = 0; i < depth; ++i) { printf("  "); }
}

static void
print_nodes(aiNode *node, u32 depth)
{
    m4x4 transform = ai_m4x4_to_m4x4(node->mTransformation);

    print_indent(depth);
    printf("%s\n", node->mName.data);

    for (s32 r = 0; r < 4; ++r)
    {
        print_indent(depth);
        for (s32 c = 0; c < 4; ++c)
        {
            printf("%f ", transform.e[r][c]);
        }
        printf("\n");
    }

    for (u32 i = 0;
         i < node->mNumChildren;
         ++i)
    {
        print_nodes(node->mChildren[i], depth + 1);
    }
}


static void 
build_skeleton(aiNode *node, s32 parent_id)
{
    assert(node);
    std::string name(node->mName.data);
    if (g_bone_map.find(name) != g_bone_map.end())
    {
        u32 id = g_bone_map.at(name);
        if (g_bone_visited.find(id) == g_bone_visited.end())
        {
            if (parent_id != -1)
            {
                g_bone_hierarchy[parent_id].push_back(id);
            }
            for (u32 child_idx = 0;
                 child_idx < node->mNumChildren;
                 ++child_idx)
            {
                aiNode *child = node->mChildren[child_idx];
                build_skeleton(child, id);
            }
            g_bone_visited[id] = 1;
        }
    }
    else
    {
        for (u32 child_idx = 0;
             child_idx < node->mNumChildren;
             ++child_idx)
        {
            aiNode *child = node->mChildren[child_idx];
            build_skeleton(child, parent_id);
        }
    }
}

inline s32
get_bone_id(aiString ai_bone_name)
{
    s32 id = 0;
    std::string bone_name(ai_bone_name.data);
    id = g_bone_map.at(bone_name);
    return id;
}

//
// IMPORTANT: This program assumes that,
//            if an identical bone-name exists between two models,
//            they have identical set of bones and a hierarchy.
//
int
main(int argc, char **argv)
{
    Memory_Arena arena = init_memory_arena();
    Asset_Model asset_model = {};

    for (u32 file_count = 0;
         file_count < 1;
         ++file_count)
    {
        const char *file_name = "xbot.dae";
        Assimp::Importer importer;
        const aiScene *model = importer.ReadFile(file_name, aiProcessPreset_TargetRealtime_Quality);

        if (model)
        {
            print_nodes(model->mRootNode, 0);
            printf("success: load model '%s'.\n", file_name);
            printf("  mesh count      : %d\n", model->mNumMeshes);
            printf("  texture count   : %d\n", model->mNumTextures);
            printf("  animation count : %d\n", model->mNumAnimations);


            const char *model_file_name = "models.pack";
            FILE *model_out = fopen(model_file_name, "wb");
            if (model_out)
            {
                std::unordered_map<s32, Asset_Bone> model_bones;

                if (model->HasMeshes())
                {
                    u32 mesh_count = model->mNumMeshes;
                    aiMesh **meshes = model->mMeshes;

                    asset_model.mesh_count    = mesh_count;
                    asset_model.meshes        = push_array(&arena, Asset_Mesh, asset_model.mesh_count);

                    for (u32 mesh_idx = 0;
                         mesh_idx < mesh_count;
                         ++mesh_idx)
                    {
                        Asset_Mesh *asset_mesh          = (asset_model.meshes + mesh_idx);
                        aiMesh *mesh                    = meshes[mesh_idx];

                        u32 vertex_count                = mesh->mNumVertices;
                        asset_mesh->vertex_count        = vertex_count;
                        asset_mesh->vertices            = push_array(&arena, Asset_Vertex, asset_mesh->vertex_count);

                        b32 mesh_has_normals = mesh->HasNormals();
                        b32 mesh_has_uvs     = mesh->HasTextureCoords(0);
                        b32 mesh_has_colors  = mesh->HasVertexColors(0);
                        b32 mesh_has_bones   = mesh->HasBones();

                        for (u32 vertex_idx = 0;
                             vertex_idx < vertex_count;
                             ++vertex_idx)
                        {
                            Asset_Vertex *asset_vertex = asset_mesh->vertices + vertex_idx;

                            asset_vertex->pos.x = mesh->mVertices[vertex_idx].x;
                            asset_vertex->pos.y = mesh->mVertices[vertex_idx].y;
                            asset_vertex->pos.z = mesh->mVertices[vertex_idx].z;

                            if (mesh_has_normals)
                            {
                                asset_vertex->normal.x = mesh->mNormals[vertex_idx].x;
                                asset_vertex->normal.y = mesh->mNormals[vertex_idx].y;
                                asset_vertex->normal.z = mesh->mNormals[vertex_idx].z;
                            }
                            else
                            {
                            }

                            if (mesh_has_uvs)
                            {
                                asset_vertex->uv.x = mesh->mTextureCoords[0][vertex_idx].x;
                                asset_vertex->uv.y = mesh->mTextureCoords[0][vertex_idx].y;
                            }
                            else
                            {
                            }

                            if (mesh_has_colors)
                            {
                                asset_vertex->color.r = mesh->mColors[0][vertex_idx].r;
                                asset_vertex->color.g = mesh->mColors[0][vertex_idx].g;
                                asset_vertex->color.b = mesh->mColors[0][vertex_idx].b;
                                asset_vertex->color.a = mesh->mColors[0][vertex_idx].a;
                            }
                            else
                            {
                                asset_vertex->color = V4(1.0f, 1.0f, 1.0f, 1.0f);
                            }

                            for (u32 i = 0; i < MAX_BONE_PER_VERTEX; ++i)
                            {
                                asset_vertex->bone_ids[i] = -1;
                            }
                        }

                        if (mesh_has_bones)
                        {
                            u32 bone_count = mesh->mNumBones;
                            for (u32 i = 0;
                                 i < bone_count;
                                 ++i)
                            {
                                aiBone *bone = mesh->mBones[i];
                                std::string bone_name(bone->mName.data);
                                m4x4        bone_offset = ai_m4x4_to_m4x4(bone->mOffsetMatrix);
                                s32         bone_id;
                                if (g_bone_map.find(bone_name) == g_bone_map.end())
                                {
                                    assert(g_bone_map_used != 100);
                                    bone_id = g_bone_map_used++;
                                    g_bone_map[bone_name] = bone_id;
                                }
                                else
                                {
                                    bone_id = g_bone_map.at(bone_name);
                                }

                                if (model_bones.find(bone_id) == model_bones.end())
                                {
                                    Asset_Bone model_bone = {};
                                    model_bones[bone_id] = model_bone;
                                }
                            }

                            // go through mesh->mBones, write offset mat4 to asset_bone
                            for (u32 bone_idx = 0;
                                 bone_idx < bone_count;
                                 ++bone_idx)
                            {
                                aiBone *bone            = mesh->mBones[bone_idx];
                                s32 bone_id             = get_bone_id(bone->mName);
                                Asset_Bone *model_bone  = &model_bones.at(bone_id);
                                model_bone->bone_id     = bone_id;
                                model_bone->offset      = ai_m4x4_to_m4x4(bone->mOffsetMatrix);
                                model_bone->transform   = ai_m4x4_to_m4x4(model->mRootNode->FindNode(bone->mName)->mTransformation);
                            }

                            // per mesh, there are vertices.
                            // per vertex, there're bone-ids and bone-weights.
                            for (u32 bone_idx = 0;
                                 bone_idx < bone_count;
                                 ++bone_idx)
                            {
                                aiBone *bone = mesh->mBones[bone_idx];
                                s32 bone_id  = get_bone_id(bone->mName);
                                for (u32 count = 0;
                                     count < bone->mNumWeights;
                                     ++count)
                                {
                                    aiVertexWeight *vw = bone->mWeights + count;
                                    u32 v  = vw->mVertexId;
                                    f32 w  = vw->mWeight;
                                    Asset_Vertex *asset_vertex = (asset_mesh->vertices + v);
                                    for (u32 i = 0;
                                         i < MAX_BONE_PER_VERTEX;
                                         ++i)
                                    {
                                        if (asset_vertex->bone_ids[i] == -1)
                                        {
                                            asset_vertex->bone_ids[i]       = bone_id;
                                            asset_vertex->bone_weights[i]   = w;
                                            break;
                                        }
                                    }
                                }
                            }

                        }
                        else
                        {
                            printf("LOG: no bones found in the mesh.\n");
                        }

                        u32 triangle_count              = mesh->mNumFaces;
                        u32 index_count                 = triangle_count * 3;
                        asset_mesh->index_count         = index_count;
                        asset_mesh->indices             = push_array(&arena, u32, asset_mesh->index_count);

                        for (u32 triangle_idx = 0;
                             triangle_idx < triangle_count;
                             ++triangle_idx)
                        {
                            aiFace *triangle = (mesh->mFaces + triangle_idx);
                            assert(triangle->mNumIndices == 3);
                            for (u32 i = 0; i < 3; ++i)
                            {
                                size_t idx_of_idx = (3 * triangle_idx + i);
                                asset_mesh->indices[idx_of_idx] = triangle->mIndices[i];
                            }
                        }
                    }



                    //
                    // write.
                    // TODO: this is janky asf.
                    //
                    fwrite(&asset_model.mesh_count, sizeof(asset_model.mesh_count), 1, model_out);

                    for (u32 mesh_idx = 0;
                         mesh_idx < asset_model.mesh_count;
                         ++mesh_idx)
                    {
                        Asset_Mesh *asset_mesh = (asset_model.meshes + mesh_idx);

                        fwrite(&asset_mesh->vertex_count, sizeof(u32), 1, model_out);
                        fwrite(asset_mesh->vertices, sizeof(Asset_Vertex) * asset_mesh->vertex_count, 1, model_out);

                        fwrite(&asset_mesh->index_count, sizeof(u32), 1, model_out);
                        fwrite(asset_mesh->indices, sizeof(u32) * asset_mesh->index_count, 1, model_out);
                    }

                    m4x4 model_root_transform           = ai_m4x4_to_m4x4(model->mRootNode->mTransformation);
                    u32  model_bone_count               = (u32)model_bones.size();
                    s32  model_root_bone_id             = get_bone_id(find_root_bone_node(model->mRootNode)->mName);
                    fwrite(&model_root_transform, sizeof(m4x4), 1, model_out);
                    fwrite(&model_bone_count, sizeof(u32), 1, model_out);
                    fwrite(&model_root_bone_id, sizeof(s32), 1, model_out);
                    for (auto &[id, model_bone] : model_bones)
                    {
                        fwrite(&model_bone.bone_id, sizeof(s32), 1, model_out);
                        fwrite(&model_bone.offset, sizeof(m4x4), 1, model_out);
                        fwrite(&model_bone.transform, sizeof(m4x4), 1, model_out);
                    }

                    printf("success: written '%s'\n", model_file_name);
                    fclose(model_out);
                }
                else
                {
                }

                if (model->HasTextures())
                {
                }
                else
                {
                }


                if (model->HasMaterials())
                {
#if 0
                    for (u32 material_index = 0;
                         material_index < model->mNumMaterials;
                         ++material_index)
                    {
                        aiMaterial *material = model->mMaterials[material_index];
                        for (u32 stack = 0; 1; ++stack)
                        {
                            aiString path;
                            material->Get(AI_MATKEY_TEXTURE(stack, aiTextureType_AMBIENT), path);
                            material->Get(AI_MATKEY_TEXTURE(stack, aiTextureType_DIFFUSE), path);
                            material->Get(AI_MATKEY_TEXTURE(stack, aiTextureType_SPECULAR), path);
                            material->Get(AI_MATKEY_TEXTURE(stack, aiTextureType_NORMALS), path);
                            material->Get(AI_MATKEY_TEXTURE(stack, aiTextureType_BASE_COLOR), path);
                            printf("msvc piece of shit.\n");
                        }
                    }
#endif
                }
                else
                {
                }

                build_skeleton(model->mRootNode, -1);


            }
            else
            {
                printf("error: couldn't open output file %s\n", model_file_name);
                exit(1);
            }


            //
            // Animation
            //
            if (model->HasAnimations())
            {
                for (u32 anim_idx = 0;
                     anim_idx < model->mNumAnimations;
                     ++anim_idx)
                {
                    aiAnimation *anim = model->mAnimations[anim_idx];
                    std::string anim_name(anim->mName.data);
                    Asset_Animation stub_anim = {};
                    g_animations.push_back(stub_anim);
                    Asset_Animation *asset_anim = &g_animations.back();
                    f32 spt = 1.0f / (f32)anim->mTicksPerSecond;

                    if (g_anim_map.find(anim_name) == g_anim_map.end())
                    {
                        s32 anim_id             = g_anim_map_used;
                        g_anim_map[anim_name]   = g_anim_map_used++;
                        f32 anim_duration       = (f32)(anim->mDuration / anim->mTicksPerSecond);
                        asset_anim->id          = anim_id;
                        asset_anim->duration    = anim_duration;
                        asset_anim->bone_count  = anim->mNumChannels;
                        asset_anim->bones       = push_array(&arena, Asset_Animation_Bone, asset_anim->bone_count);

                        for (u32 bone_idx = 0;
                             bone_idx < anim->mNumChannels;
                             ++bone_idx)
                        {
                            aiNodeAnim *bone = anim->mChannels[bone_idx];
                            Asset_Animation_Bone *asset_bone = (asset_anim->bones + bone_idx);
                            asset_bone->bone_id             = get_bone_id(bone->mNodeName);
                            asset_bone->translation_count   = bone->mNumPositionKeys;
                            asset_bone->rotation_count      = bone->mNumRotationKeys;
                            asset_bone->scaling_count       = bone->mNumScalingKeys;
                            asset_bone->translations        = push_array(&arena, dt_v3_Pair, asset_bone->translation_count);
                            asset_bone->rotations           = push_array(&arena, dt_qt_Pair, asset_bone->rotation_count);
                            asset_bone->scalings            = push_array(&arena, dt_v3_Pair, asset_bone->scaling_count);
                            for (u32 idx = 0;
                                 idx < asset_bone->translation_count;
                                 ++idx)
                            {
                                dt_v3_Pair *asset_translation = (asset_bone->translations + idx);
                                aiVectorKey *key = bone->mPositionKeys + idx;
                                asset_translation->dt   = (f32)key->mTime * spt;
                                asset_translation->vec  = aiv3_to_v3(key->mValue);
                            }
                            for (u32 idx = 0;
                                 idx < asset_bone->rotation_count;
                                 ++idx)
                            {
                                dt_qt_Pair *asset_rotation = (asset_bone->rotations + idx);
                                aiQuatKey *key = bone->mRotationKeys + idx;
                                asset_rotation->dt  = (f32)key->mTime * spt;
                                asset_rotation->q   = aiqt_to_qt(key->mValue);
                            }
                            for (u32 idx = 0;
                                 idx < asset_bone->scaling_count;
                                 ++idx)
                            {
                                dt_v3_Pair *asset_scaling = (asset_bone->scalings + idx);
                                aiVectorKey *key = bone->mScalingKeys + idx;
                                asset_scaling->dt   = (f32)key->mTime * spt;
                                asset_scaling->vec  = aiv3_to_v3(key->mValue);
                            }
                        }
                    }
                    else
                    {
                    }
                }
            }


        }
        else
        {
            printf("error: couldn't load model %s.\n", file_name);
            exit(1);
        }
    }


    //
    // Global Bone Hierarchy
    //
    Asset_Bone_Hierarchy asset_bone_hierarchy = {};
    for (s32 bone_id = 0;
         bone_id < MAX_BONE_PER_MESH;
         ++bone_id)
    {
        std::vector<s32> bone_info  = g_bone_hierarchy[bone_id];
        u32 child_count             = (u32)bone_info.size();
        Asset_Bone_Info *asset_bone = &asset_bone_hierarchy.bone_infos[bone_id];
        asset_bone->child_count     = child_count;
        asset_bone->child_ids       = push_array(&arena, s32, child_count);
        for (u32 child_id = 0;
             child_id < child_count;
             ++child_id)
        {
            asset_bone->child_ids[child_id] = bone_info[child_id];
        }
    }

    const char *bones_file_name = "bones.pack";
    FILE *bones_out = fopen(bones_file_name, "wb");
    if (bones_out)
    {
        for (u32 count = 0;
             count < MAX_BONE_PER_MESH;
             ++count)
        {
            Asset_Bone_Info *asset_bone = &asset_bone_hierarchy.bone_infos[count];
            fwrite(&asset_bone->child_count, sizeof(u32), 1, bones_out);
            fwrite(asset_bone->child_ids, sizeof(s32) * asset_bone->child_count, 1, bones_out);
        }

        fclose(bones_out);
        printf("success: written '%s'.\n", bones_file_name);
    }
    else
    {
        printf("error: couldn't open file %s.\n", bones_file_name);
        exit(1);
    }

    //
    // Global Animations
    //
    const char *anim_file_name = "animations.pack";
    FILE *anim_out = fopen(anim_file_name, "wb");
    if (anim_out)
    {
        for (Asset_Animation asset_anim : g_animations)
        {
            fwrite(&asset_anim.id, sizeof(s32), 1, anim_out);
            fwrite(&asset_anim.duration, sizeof(f32), 1, anim_out);
            fwrite(&asset_anim.bone_count, sizeof(u32), 1, anim_out);
            for (u32 bone_idx = 0;
                 bone_idx < asset_anim.bone_count;
                 ++bone_idx)
            {
                Asset_Animation_Bone *asset_bone = (asset_anim.bones + bone_idx);
                fwrite(&asset_bone->bone_id, sizeof(s32), 1, anim_out);

                fwrite(&asset_bone->translation_count, sizeof(u32), 1, anim_out);
                fwrite(&asset_bone->rotation_count,    sizeof(u32), 1, anim_out);
                fwrite(&asset_bone->scaling_count,     sizeof(u32), 1, anim_out);

                fwrite(asset_bone->translations, sizeof(dt_v3_Pair) * asset_bone->translation_count, 1, anim_out);
                fwrite(asset_bone->rotations,    sizeof(dt_qt_Pair) * asset_bone->rotation_count,    1, anim_out);
                fwrite(asset_bone->scalings,     sizeof(dt_v3_Pair) * asset_bone->scaling_count,     1, anim_out);
            }
        }

        fclose(anim_out);
        printf("success: written '%s'.\n", anim_file_name);
    }
    else
    {
        printf("error: couldn't open file '%s' to write.\n", anim_file_name);
        exit(1);
    }

    printf("*** SUCCESSFUL! ***\n");
    return 0;
}
