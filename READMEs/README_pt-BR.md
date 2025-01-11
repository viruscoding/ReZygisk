# ReZygisk

[English](../README.md)

ReZygisk é uma fork do Zygisk Next, uma implementação do Zygisk independente, fornecendo a API do Zygisk para o kernelSU, Magisk (além do embutido) e APatch (a ser desenvolvido).

Ele foca em modernizar e re-escrever o código fonte para C (de C++ e Rust), permitindo uma implementação da API do Zygisk com uma licença mais permissiva.

> [!NOTE]
> Este módulo/fork ainda está em processo de desenvolvimento; apenas use .zip da aba Releases.
>
> Apesar de você poder instalar um .zip da aba [Actions](https://github.com/PerformanC/ReZygisk/actions), é de sua conta e risco, já que pode acarretar em um bootloop.

## Por quê?

Os últimos lançamentos do Zygisk Next não possuem código aberto, reservando-o para os seus desenvolvedores. Isso não só limita nossa capacidade de contribuir com o projeto, mas também impossibilita a auditoria do código, uma preocupação grave de segurança, já que o Zygisk Next é um módulo que roda como superuser (super usuário/root), tendo acesso a todo o sistema.

Os desenvolvedores do Zygisk Next são famosos e confiados pela comunidade Android, mas isso não significa que o código não seja nem malicioso nem vulnerável. Nós (PerformanC) reconhecemos seus motivos de manterem o código recluso a eles, mas a gente acredita no contrário.

## Vantagens

- FOSS (Pra sempre)

## Dependências

| Ferramenta      | Descrição                                  |
|-----------------|--------------------------------------------|
| `Android NDK`   | Kit de Desenvolvimento Nativo para Android |

### Dependências C++

| Dependência | Descrição                     |
|-------------|-------------------------------|
| `lsplt`     | PLT Hook simples para Android  |

## Forma de uso

Estamos ainda em processo de desenvolvimento desta aba. (Em breve)

## Processo de instalação

Estamos ainda em processo de desenvolvimento desta aba. (Em breve)

## Tradução

Até agora, a gente não possui uma plataforma para traduções, mas você pode contribuir para a branch [add/new-webui](https://github.com/PerformanC/ReZygisk/tree/add/new-webui). Por favor, não esqueça de incluir seu perfil do GitHub no [TRANSLATOR.md](https://github.com/PerformanC/ReZygisk/blob/add/new-webui/TRANSLATOR.md) para que assim outras pessoas vejam sua contribuição.

## Suporte

Para quaisquer problemas no ReZygisk ou qualquer projeto da PerformanC, sinta-se livre para entrar em qualquer canal abaixo:

- Server do Discord: [PerformanC](https://discord.gg/uPveNfTuCJ)
- Canal do Telegram ReZygisk: [@rezygiskchat](https://t.me/rezygiskchat)
- Canal do Telegram PerformanC: [@performancorg](https://t.me/performancorg)

## Contribuição

É obrigatório seguir as [Regras de Contribuição](https://github.com/PerformanC/contributing) da PerformanC para contribuir ao ReZygisk, seguindo sua Política de Segurança, Código de Conduta, e padronização de sintaxe.

## Licença

ReZygisk é majoritamente licenciado em GPL, por Dr-TSNG, mas também em AGPL 3.0, pela A Organização PerformanC (The PerformanC Organization) para código re-escrito. Você pode ler mais em [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).
