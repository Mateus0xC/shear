# Plugin do Valorant: valorant.dll (+ plugin.json) copiados para <pasta do shear>/plugins/valorant/ no build.
# O instalador do shear NÃO leva essa pasta: o plugin é baixado pelo app quando o usuário quiser.
add_library(valorant MODULE
  ValorantPlugin.cpp ValorantPlugin.h
  ValorantService.cpp ValorantService.h
  ValorantStore.cpp ValorantStore.h)
target_include_directories(valorant PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(valorant PRIVATE shear_valorant_core Qt6::Gui Qt6::Network)
set_target_properties(valorant PROPERTIES PREFIX "")

# A DLL e o plugin.json vão para <pasta do shear>/plugins/valorant/. O plugin.json é copiado a CADA build (alvo sempre executado):
# sem isso, mudar só o texto do plugin.json não chegava à pasta do build e o catálogo saía com a descrição antiga.
add_custom_target(valorant_files ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:shear>/plugins/valorant
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:valorant> $<TARGET_FILE_DIR:shear>/plugins/valorant/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/plugin.json $<TARGET_FILE_DIR:shear>/plugins/valorant/
  DEPENDS valorant
  COMMENT "Copiando o plugin do Valorant para ao lado do shear")
