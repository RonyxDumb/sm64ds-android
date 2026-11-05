"""Complete original Player animation and skeletal-model update bodies."""
import re
from pathlib import Path
from native_sources import function, replace_once

ROOT = Path(__file__).resolve().parents[2]


def slices(projected):
    rel = 'src/game/player/Player.cpp'
    raw = (ROOT/rel).read_text()
    advance = function(raw, 'void Player_AdvanceAnims(char *self)\n{')
    advance = advance.replace('Player *player = reinterpret_cast<Player *>(self);', 'char *player = self;')
    advance = advance.replace('player->GetBodyModelID(', '_ZNK6Player14GetBodyModelIDEjb(player,')
    advance = advance.replace('dExtAnmModel_c *model = player->mModels.mBodyModels[modelID];',
                              'void *model = ((void **)(player+0xdc))[modelID];')
    advance = advance.replace('model->Advance()', '_ZN9Animation7AdvanceEv((char *)model+0x50)')
    advance = advance.replace('player->IsAnim(', '_ZN6Player6IsAnimEj(player,')
    advance = advance.replace('player->mModels.mTexSeqBody[player->param1].currFrame',
                              '(*(s32 *)(player+0x1dc+player->param1*0x14+8))')
    advance = advance.replace('player->mModels.mTexSeqBody[player->param1].Advance()',
                              '_ZN9Animation7AdvanceEv(player+0x1dc+player->param1*0x14)')
    advance = advance.replace('player->param1', '(*(u32 *)(player+8))')

    is_anim = replace_once(function(raw, 'int Player::IsAnim(unsigned int a)'),
                           'int Player::IsAnim(unsigned int a)',
                           'int _ZN6Player6IsAnimEj(void *object,unsigned int a)')
    is_anim = is_anim.replace('this->GetBodyModelID(', '_ZNK6Player14GetBodyModelIDEjb(object,')
    is_anim = is_anim.replace('dExtAnmModel_c *model = mModels.mBodyModels[id];',
                              'void *model = ((void **)((char *)object+0xdc))[id];')
    is_anim = is_anim.replace('model->file', '(*(void **)((char *)model+0x60))')
    is_anim = re.sub(r'\bparam1\b', '(*(u32 *)((char *)object+8))', is_anim)
    row, bindings = projected(rel, advance+'\n'+is_anim, '''
u32 _ZNK6Player14GetBodyModelIDEjb(void *,u32,int);
int _ZN6Player6IsAnimEj(void *,u32);
void _ZN9Animation7AdvanceEv(void *);
''', ['Player_AdvanceAnims','_ZN6Player6IsAnimEj'], auto_globals=False)
    row['unit'] = 'stage_player_advance_anims'
    for binding in bindings:
        binding['overlay'] = 2
    yield row, bindings

    rel = 'src/engine/model/d_ext_anm_model.cpp'
    raw = (ROOT/rel).read_text()
    bodies = []
    for signature, export, params in [
        ('void dExtAnmModel_c::UpdateVerts()', '_ZN14dExtAnmModel_c11UpdateVertsEv', 'void *object'),
        ('void dExtAnmModel_c::Render(const Vector3 *scale)',
         '_ZN14dExtAnmModel_c6RenderEPK7Vector3', 'void *object,void *scale')]:
        body = replace_once(function(raw, signature), signature, 'void '+export+'('+params+')')
        body = body.replace('currFrame', '(*(s32 *)((char *)object+0x58))')
        body = body.replace('data.UpdateBones(file,',
                            '_ZN15ModelComponents11UpdateBonesEP8BCA_Filei((char *)object+8,(*(void **)((char *)object+0x60)),')
        body = body.replace('data.UpdateVertsUsingBones()',
                            '_ZN15ModelComponents21UpdateVertsUsingBonesEv((char *)object+8)')
        body = body.replace('UpdateVerts()', '_ZN14dExtAnmModel_c11UpdateVertsEv(object)')
        body = body.replace('dExtSimpleModel_c::Render(scale)',
                            '_ZN17dExtSimpleModel_c6RenderEPK7Vector3(object,scale)')
        bodies.append(body)
    row, bindings = projected(rel, '\n'.join(bodies), '''
void _ZN15ModelComponents11UpdateBonesEP8BCA_Filei(void *,void *,int);
void _ZN15ModelComponents21UpdateVertsUsingBonesEv(void *);
void _ZN14dExtAnmModel_c11UpdateVertsEv(void *);
void _ZN17dExtSimpleModel_c6RenderEPK7Vector3(void *,void *);
''', ['_ZN14dExtAnmModel_c11UpdateVertsEv','_ZN14dExtAnmModel_c6RenderEPK7Vector3'], auto_globals=False)
    row['unit'] = 'stage_animated_model_render'
    yield row, bindings
